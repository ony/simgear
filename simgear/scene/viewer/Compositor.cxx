// Copyright (C) 2018  Fernando García Liñán <fernandogarcialinan@gmail.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA

#include "Compositor.hxx"

#include <algorithm>

#include <osgUtil/IntersectionVisitor>

#include <osgViewer/Renderer>
#include <osgViewer/Viewer>

#include <simgear/math/SGRect.hxx>
#include <simgear/props/props_io.hxx>
#include <simgear/scene/material/EffectCullVisitor.hxx>
#include <simgear/scene/util/SGReaderWriterOptions.hxx>
#include <simgear/scene/util/RenderConstants.hxx>
#include <simgear/structure/exception.hxx>

#include "CompositorUtil.hxx"

namespace simgear {
namespace compositor {

Compositor *
Compositor::create(osg::View *view,
                   osg::GraphicsContext *gc,
                   osg::Viewport *viewport,
                   const SGPropertyNode *property_list,
                   const SGReaderWriterOptions *options)
{
    osg::ref_ptr<Compositor> compositor = new Compositor(view, gc, viewport);
    compositor->_name = property_list->getStringValue("name");

    // Read all buffers first so passes can use them
    PropertyList p_buffers = property_list->getChildren("buffer");
    for (auto const &p_buffer : p_buffers) {
        if (!checkConditional(p_buffer))
            continue;
        const std::string &buffer_name = p_buffer->getStringValue("name");
        if (buffer_name.empty()) {
            SG_LOG(SG_INPUT, SG_ALERT, "Compositor::build: Buffer requires "
                   "a name to be available to passes. Skipping...");
            continue;
        }
        Buffer *buffer = buildBuffer(compositor.get(), p_buffer, options);
        if (buffer)
            compositor->addBuffer(buffer_name, buffer);
    }
    // Read passes
    PropertyList p_passes = property_list->getChildren("pass");
    for (auto const &p_pass : p_passes) {
        if (!checkConditional(p_pass))
            continue;
        Pass *pass = buildPass(compositor.get(), p_pass, options);
        if (pass)
            compositor->addPass(pass);
    }

    return compositor.release();
}

Compositor *
Compositor::create(osg::View *view,
                   osg::GraphicsContext *gc,
                   osg::Viewport *viewport,
                   const std::string &name,
                   const SGReaderWriterOptions *options)
{
    std::string filename(name);
    filename += ".xml";
    std::string abs_filename = SGModelLib::findDataFile(filename);
    if (abs_filename.empty()) {
        SG_LOG(SG_INPUT, SG_ALERT, "Compositor::build: Could not find file '"
               << filename << "'");
        return 0;
    }

    SGPropertyNode_ptr property_list = new SGPropertyNode;
    try {
        readProperties(abs_filename, property_list.ptr(), 0, true);
    } catch (sg_io_exception &e) {
        SG_LOG(SG_INPUT, SG_ALERT, "Compositor::build: Failed to parse file '"
               << abs_filename << "'. " << e.getFormattedMessage());
        return 0;
    }

    return create(view, gc, viewport, property_list, options);
}

Compositor::Compositor(osg::View *view,
                       osg::GraphicsContext *gc,
                       osg::Viewport *viewport) :
    _view(view),
    _gc(gc),
    _viewport(viewport),
    _uniforms{
    new osg::Uniform("fg_ViewportSize", osg::Vec2f()),
    new osg::Uniform("fg_ViewMatrix", osg::Matrixf()),
    new osg::Uniform("fg_ViewMatrixInverse", osg::Matrixf()),
    new osg::Uniform("fg_ProjectionMatrix", osg::Matrixf()),
    new osg::Uniform("fg_ProjectionMatrixInverse", osg::Matrixf()),
    new osg::Uniform("fg_CameraPositionCart", osg::Vec3f()),
    new osg::Uniform("fg_CameraPositionGeod", osg::Vec3f())
    }
{
}

Compositor::~Compositor()
{
}

void
Compositor::update(const osg::Matrix &view_matrix,
                   const osg::Matrix &proj_matrix)
{
    for (auto &pass : _passes) {
        if (pass->inherit_cull_mask) {
            osg::Camera *camera = pass->camera;
            osg::Camera *view_camera = _view->getCamera();
            camera->setCullMask(pass->cull_mask
                                & view_camera->getCullMask());
            camera->setCullMaskLeft(pass->cull_mask
                                    & view_camera->getCullMaskLeft());
            camera->setCullMaskRight(pass->cull_mask
                                     & view_camera->getCullMaskRight());
        }

        if (pass->update_callback.valid())
            pass->update_callback->updatePass(*pass.get(), view_matrix, proj_matrix);
    }

    // Update uniforms
    osg::Matrixd view_inverse = osg::Matrix::inverse(view_matrix);
    osg::Vec4d camera_pos = osg::Vec4(0.0, 0.0, 0.0, 1.0) * view_inverse;
    SGGeod camera_pos_geod = SGGeod::fromCart(
        SGVec3d(camera_pos.x(), camera_pos.y(), camera_pos.z()));

    for (int i = 0; i < TOTAL_BUILTIN_UNIFORMS; ++i) {
        osg::ref_ptr<osg::Uniform> u = _uniforms[i];
        switch (i) {
        case VIEWPORT_SIZE:
            u->set(osg::Vec2f(_viewport->width(), _viewport->height()));
            break;
        case VIEW_MATRIX:
            u->set(view_matrix);
            break;
        case VIEW_MATRIX_INV:
            u->set(view_inverse);
            break;
        case PROJECTION_MATRIX:
            u->set(proj_matrix);
            break;
        case PROJECTION_MATRIX_INV:
            u->set(osg::Matrix::inverse(proj_matrix));
            break;
        case CAMERA_POSITION_CART:
            u->set(osg::Vec3f(camera_pos.x(), camera_pos.y(), camera_pos.z()));
            break;
        case CAMERA_POSITION_GEOD:
            u->set(osg::Vec3f(camera_pos_geod.getLongitudeRad(),
                              camera_pos_geod.getLatitudeRad(),
                              camera_pos_geod.getElevationM()));
            break;
        default:
            // Unknown uniform
            break;
        }
    }
}

void
Compositor::resized()
{
    // Cameras attached directly to the framebuffer were already resized by
    // osg::GraphicsContext::resizedImplementation(). However, RTT cameras were
    // ignored. Here we resize RTT cameras that need to match the physical
    // viewport size.
    for (const auto &pass : _passes) {
        osg::Camera *camera = pass->camera;
        if (!camera->isRenderToTextureCamera()  ||
            pass->viewport_width_scale  == 0.0f ||
            pass->viewport_height_scale == 0.0f)
            continue;

        // Resize both the viewport and its texture attachments
        camera->resize(pass->viewport_width_scale  * _viewport->width(),
                       pass->viewport_height_scale * _viewport->height());
    }
}

void
Compositor::addBuffer(const std::string &name, Buffer *buffer)
{
    _buffers[name] = buffer;
}

void
Compositor::addPass(Pass *pass)
{
    if (!_view) {
        SG_LOG(SG_GENERAL, SG_ALERT, "Compositor::addPass: Couldn't add camera "
               "as a slave to the view. View doesn't exist!");
        return;
    }

    _view->addSlave(pass->camera, pass->useMastersSceneData);

    // Install the Effect cull visitor
    osgViewer::Renderer* renderer
        = static_cast<osgViewer::Renderer*>(pass->camera->getRenderer());
    for (int i = 0; i < 2; ++i) {
        osgUtil::SceneView* sceneView = renderer->getSceneView(i);

        osg::ref_ptr<osgUtil::CullVisitor::Identifier> identifier;
        identifier = sceneView->getCullVisitor()->getIdentifier();

        sceneView->setCullVisitor(
            new EffectCullVisitor(false, pass->effect_scheme));
        sceneView->getCullVisitor()->setIdentifier(identifier.get());

        identifier = sceneView->getCullVisitorLeft()->getIdentifier();
        sceneView->setCullVisitorLeft(sceneView->getCullVisitor()->clone());
        sceneView->getCullVisitorLeft()->setIdentifier(identifier.get());

        identifier = sceneView->getCullVisitorRight()->getIdentifier();
        sceneView->setCullVisitorRight(sceneView->getCullVisitor()->clone());
        sceneView->getCullVisitorRight()->setIdentifier(identifier.get());
    }

    _passes.push_back(pass);
}

Buffer *
Compositor::getBuffer(const std::string &name) const
{
    auto it = _buffers.find(name);
    if (it == _buffers.end())
        return 0;
    return it->second.get();
}

Pass *
Compositor::getPass(const std::string &name) const
{
    auto it = std::find_if(_passes.begin(), _passes.end(),
                           [&name](const osg::ref_ptr<Pass> &p) {
                               return p->name == name;
                           });
    if (it == _passes.end())
        return 0;
    return (*it);
}

} // namespace compositor
} // namespace simgear
