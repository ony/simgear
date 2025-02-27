///@file
/// A text on the Canvas
//
// Copyright (C) 2012  Thomas Geymayer <tomgey@gmail.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA

#include <simgear_config.h>

#include "CanvasText.hxx"
#include <simgear/canvas/Canvas.hxx>
#include <simgear/canvas/CanvasSystemAdapter.hxx>
#include <simgear/scene/util/parse_color.hxx>
#include <osg/Version>
#include <osgDB/Registry>
#include <osgText/Text>

namespace simgear
{
namespace canvas
{
  class Text::TextOSG:
    public osgText::Text
  {
    public:
      TextOSG(canvas::Text* text);

      void setFontResolution(int res);
      void setCharacterAspect(float aspect);
      void setLineHeight(float factor);
      void setFill(const std::string& fill);
      void setStroke(const std::string& color);
      void setBackgroundColor(const std::string& fill);

      float lineHeight() const;

      /// Get the number of lines
      size_t lineCount() const;

      /// Get line @a i
      TextLine lineAt(size_t i) const;

      /// Get nearest line to given y-coordinate
#if OSG_VERSION_LESS_THAN(3,6,5)
      TextLine nearestLine(float pos_y) const;
      SGVec2i sizeForWidth(int w) const;
#else
      TextLine nearestLine(float pos_y);
      SGVec2i sizeForWidth(int w);
#endif

      osg::BoundingBox
#if OSG_VERSION_LESS_THAN(3,3,2)
      computeBound()
#else
      computeBoundingBox()
#endif
      const override;

    protected:
      friend class TextLine;

      canvas::Text *_text_element;

#if OSG_VERSION_LESS_THAN(3,5,6)
     void computePositions(unsigned int contextID) const override;
#else
     void computePositionsImplementation() override;
#endif
};

  class TextLine
  {
    public:
      TextLine();
      TextLine(size_t line, Text::TextOSG const* text);

      /// Number of characters on this line
      size_t size() const;
      bool empty() const;

      osg::Vec2 cursorPos(size_t i) const;
      osg::Vec2 nearestCursor(float x) const;

    protected:
      typedef Text::TextOSG::GlyphQuads GlyphQuads;

      Text::TextOSG const *_text;
      GlyphQuads const    *_quads;

      size_t _line,
             _begin,
             _end;
  };

  //----------------------------------------------------------------------------
  TextLine::TextLine():
    _text(NULL),
    _quads(NULL),
    _line(0),
    _begin(-1),
    _end(-1)
  {

  }

  //----------------------------------------------------------------------------
  TextLine::TextLine(size_t line, Text::TextOSG const* text):
    _text(text),
    _quads(NULL),
    _line(line),
    _begin(-1),
    _end(-1)
  {
    if( !text || text->_textureGlyphQuadMap.empty() || !_text->lineCount() )
      return;

    _quads = &text->_textureGlyphQuadMap.begin()->second;


#if OSG_VERSION_LESS_THAN(3,5,6)
    GlyphQuads::LineNumbers const& line_numbers = _quads->_lineNumbers;
    GlyphQuads::LineNumbers::const_iterator begin_it =
      std::lower_bound(line_numbers.begin(), line_numbers.end(), _line);

    if( begin_it == line_numbers.end() || *begin_it != _line )
      // empty line or past last line
      return;

    _begin = begin_it - line_numbers.begin();
    _end = std::upper_bound(begin_it, line_numbers.end(), _line)
         - line_numbers.begin();
#else
//OSG:TODO: Need 3.5.6 version of this

#endif
  }

  //----------------------------------------------------------------------------
  size_t TextLine::size() const
  {
    return _end - _begin;
  }

  //----------------------------------------------------------------------------
  bool TextLine::empty() const
  {
    return _end == _begin;
  }

  //----------------------------------------------------------------------------
  osg::Vec2 TextLine::cursorPos(size_t i) const
  {
    if( !_quads )
      return osg::Vec2(0, 0);

    if( i > size() )
      // Position after last character if out of range (TODO better exception?)
      i = size();

    osg::Vec2 pos(0, _text->_offset.y() + _line * _text->lineHeight());

    if( empty() )
      return pos;
#if OSG_VERSION_LESS_THAN(3,3,5)
      GlyphQuads::Coords2 const& coords = _quads->_coords;
#elif OSG_VERSION_LESS_THAN(3,5,6)
      GlyphQuads::Coords2 refCoords = _quads->_coords;
      GlyphQuads::Coords2::element_type &coords = *refCoords.get();
      size_t global_i = _begin + i;

      if (global_i == _begin)
          // before first character of line
          pos.x() = coords[_begin * 4].x();
      else if (global_i == _end)
          // After Last character of line
          pos.x() = coords[(_end - 1) * 4 + 2].x();
      else
      {
          float prev_l = coords[(global_i - 1) * 4].x(),
              prev_r = coords[(global_i - 1) * 4 + 2].x(),
              cur_l = coords[global_i * 4].x();

          if (prev_l == prev_r)
              // If previous character width is zero set to begin of next character
              // (Happens eg. with spaces)
              pos.x() = cur_l;
          else
              // position at center between characters
              pos.x() = 0.5 * (prev_r + cur_l);
      }
#else
//OSG:TODO: need 3.5.7 version of this.
#endif

    return pos;
  }

  //----------------------------------------------------------------------------
  osg::Vec2 TextLine::nearestCursor(float x) const
  {
    if( empty() )
      return cursorPos(0);

    GlyphQuads::Glyphs const& glyphs = _quads->_glyphs;
#if OSG_VERSION_LESS_THAN(3,3,5)
    GlyphQuads::Coords2 const& coords = _quads->_coords;
#elif OSG_VERSION_LESS_THAN(3,5,6)

    GlyphQuads::Coords2 refCoords = _quads->_coords;
    GlyphQuads::Coords2::element_type &coords = *refCoords.get();

    float const HIT_FRACTION = 0.6;
    float const character_width = _text->getCharacterHeight()
                                * _text->getCharacterAspectRatio();

    size_t i = _begin;
    for(; i < _end; ++i)
    {
      // Get threshold for mouse x position for setting cursor before or after
      // current character
      float threshold = coords[i * 4].x()
                      + HIT_FRACTION * glyphs[i]->getHorizontalAdvance()
                                     * character_width;

      if( x <= threshold )
        break;
    }

    return cursorPos(i - _begin);
#else
//OSG:TODO: need 3.5.7 version of this.
	return cursorPos(0);
#endif
  }

  //----------------------------------------------------------------------------
  Text::TextOSG::TextOSG(canvas::Text* text):
    _text_element(text)
  {
    setBackdropImplementation(NO_DEPTH_BUFFER);
  }

  //----------------------------------------------------------------------------
  void Text::TextOSG::setFontResolution(int res)
  {
    TextBase::setFontResolution(res, res);
  }

  //----------------------------------------------------------------------------
  void Text::TextOSG::setCharacterAspect(float aspect)
  {
    setCharacterSize(getCharacterHeight(), aspect);
  }

  //----------------------------------------------------------------------------
  void Text::TextOSG::setLineHeight(float factor)
  {
    setLineSpacing(factor - 1);
  }

  //----------------------------------------------------------------------------
  void Text::TextOSG::setFill(const std::string& fill)
  {
//    if( fill == "none" )
//      TODO No text
//    else
    osg::Vec4 color;
    if( parseColor(fill, color) )
      setColor( color );
  }

  //----------------------------------------------------------------------------
  void Text::TextOSG::setStroke(const std::string& stroke)
  {
    osg::Vec4 color;
    if( stroke == "none" || !parseColor(stroke, color) )
      setBackdropType(NONE);
    else
    {
      setBackdropType(OUTLINE);
      setBackdropColor(color);
    }
  }

  //----------------------------------------------------------------------------
  void Text::TextOSG::setBackgroundColor(const std::string& fill)
  {
    osg::Vec4 color;
    if( parseColor(fill, color) )
      setBoundingBoxColor( color );
  }

  //----------------------------------------------------------------------------
  float Text::TextOSG::lineHeight() const
  {
    return (1 + _lineSpacing) * _characterHeight;
  }

  //----------------------------------------------------------------------------
  size_t Text::TextOSG::lineCount() const
  {
    return _lineCount;
  }

  //----------------------------------------------------------------------------
  TextLine Text::TextOSG::lineAt(size_t i) const
  {
    return TextLine(i, this);
  }

  //----------------------------------------------------------------------------
#if OSG_VERSION_LESS_THAN(3,6,5)
  TextLine Text::TextOSG::nearestLine(float pos_y) const
  {
    osgText::Font const* font = getActiveFont();
#else
  TextLine Text::TextOSG::nearestLine(float pos_y)
  {
    auto font = getActiveFont();
#endif

    if( !font || lineCount() <= 0 )
      return TextLine(0, this);

    float asc = .9f, desc = -.2f;
    font->getVerticalSize(asc, desc);

    float first_line_y = _offset.y()
                       - (1 + _lineSpacing / 2 + desc) * _characterHeight;

    size_t line_num = std::min<size_t>(
      std::max<size_t>(0, (pos_y - first_line_y) / lineHeight()),
      lineCount() - 1
    );

    return TextLine(line_num, this);
  }

  //----------------------------------------------------------------------------
  // simplified version of osgText::Text::computeGlyphRepresentation() to
  // just calculate the size for a given weight. Glpyh calculations/creating
  // is not necessary for this...
#if OSG_VERSION_LESS_THAN(3,6,5)
  SGVec2i Text::TextOSG::sizeForWidth(int w) const
#else
  SGVec2i Text::TextOSG::sizeForWidth(int w)
#endif
  {
    if( _text.empty() )
      return SGVec2i(0, 0);

#if OSG_VERSION_LESS_THAN(3,6,5)
    osgText::Font* activefont = const_cast<osgText::Font*>(getActiveFont());
#else
    auto activefont = getActiveFont();
#endif

    if( !activefont )
      return SGVec2i(-1, -1);

    float max_width_safe = _maximumWidth;
    const_cast<TextOSG*>(this)->_maximumWidth = w;

    SGRecti bb;

    osg::Vec2 startOfLine_coords(0.0f,0.0f);
    osg::Vec2 cursor(startOfLine_coords);
    osg::Vec2 local(0.0f,0.0f);

    unsigned int previous_charcode = 0;
    unsigned int line_length = 0;
    bool horizontal = _layout != VERTICAL;
    bool kerning = true;

    float hr = _characterHeight;
    float wr = hr / getCharacterAspectRatio();

    // osg should really care more about const :-/
    osgText::String& text = const_cast<osgText::String&>(_text);
    typedef osgText::String::iterator TextIterator;

    for( TextIterator itr = text.begin(); itr != text.end(); )
    {
      // record the start of the current line
      TextIterator startOfLine_itr = itr;

      // find the end of the current line.
      osg::Vec2 endOfLine_coords(cursor);
      TextIterator endOfLine_itr =
        const_cast<TextOSG*>(this)->computeLastCharacterOnLine(
          endOfLine_coords, itr, text.end()
        );

      line_length = endOfLine_itr - startOfLine_itr;

      // Set line position to correct alignment.
      switch( _layout )
      {
        case LEFT_TO_RIGHT:
        {
          switch( _alignment )
          {
            // nothing to be done for these
            //case LEFT_TOP:
            //case LEFT_CENTER:
            //case LEFT_BOTTOM:
            //case LEFT_BASE_LINE:
            //case LEFT_BOTTOM_BASE_LINE:
            //  break;
            case CENTER_TOP:
            case CENTER_CENTER:
            case CENTER_BOTTOM:
            case CENTER_BASE_LINE:
            case CENTER_BOTTOM_BASE_LINE:
              cursor.x() = (cursor.x() - endOfLine_coords.x()) * 0.5f;
              break;
            case RIGHT_TOP:
            case RIGHT_CENTER:
            case RIGHT_BOTTOM:
            case RIGHT_BASE_LINE:
            case RIGHT_BOTTOM_BASE_LINE:
              cursor.x() = cursor.x() - endOfLine_coords.x();
              break;
            default:
              break;
          }
          break;
        }
        case RIGHT_TO_LEFT:
        {
          switch( _alignment )
          {
            case LEFT_TOP:
            case LEFT_CENTER:
            case LEFT_BOTTOM:
            case LEFT_BASE_LINE:
            case LEFT_BOTTOM_BASE_LINE:
              cursor.x() = 2 * cursor.x() - endOfLine_coords.x();
              break;
            case CENTER_TOP:
            case CENTER_CENTER:
            case CENTER_BOTTOM:
            case CENTER_BASE_LINE:
            case CENTER_BOTTOM_BASE_LINE:
              cursor.x() = cursor.x()
                  + (cursor.x() - endOfLine_coords.x()) * 0.5f;
              break;
              // nothing to be done for these
              //case RIGHT_TOP:
              //case RIGHT_CENTER:
              //case RIGHT_BOTTOM:
              //case RIGHT_BASE_LINE:
              //case RIGHT_BOTTOM_BASE_LINE:
              //  break;
            default:
              break;
          }
          break;
        }
        case VERTICAL:
        {
          switch( _alignment )
          {
            // TODO: current behaviour top baselines lined up in both cases - need to implement
            //       top of characters alignment - Question is this necessary?
            // ... otherwise, nothing to be done for these 6 cases
            //case LEFT_TOP:
            //case CENTER_TOP:
            //case RIGHT_TOP:
            //  break;
            //case LEFT_BASE_LINE:
            //case CENTER_BASE_LINE:
            //case RIGHT_BASE_LINE:
            //  break;
            case LEFT_CENTER:
            case CENTER_CENTER:
            case RIGHT_CENTER:
              cursor.y() = cursor.y()
                  + (cursor.y() - endOfLine_coords.y()) * 0.5f;
              break;
            case LEFT_BOTTOM_BASE_LINE:
            case CENTER_BOTTOM_BASE_LINE:
            case RIGHT_BOTTOM_BASE_LINE:
              cursor.y() = cursor.y() - (line_length * _characterHeight);
              break;
            case LEFT_BOTTOM:
            case CENTER_BOTTOM:
            case RIGHT_BOTTOM:
              cursor.y() = 2 * cursor.y() - endOfLine_coords.y();
              break;
            default:
              break;
          }
          break;
        }
      }

      if( itr != endOfLine_itr )
      {

        for(;itr != endOfLine_itr;++itr)
        {
          unsigned int charcode = *itr;

          osgText::Glyph* glyph = activefont->getGlyph(_fontSize, charcode);
          if( glyph )
          {
            float width = (float) (glyph->getWidth()) * wr;
            float height = (float) (glyph->getHeight()) * hr;

            if( _layout == RIGHT_TO_LEFT )
            {
              cursor.x() -= glyph->getHorizontalAdvance() * wr;
            }

            // adjust cursor position w.r.t any kerning.
            if( kerning && previous_charcode )
            {
              switch( _layout )
              {
                case LEFT_TO_RIGHT:
                {
#if OSG_VERSION_LESS_THAN(3,5,2)
                    osg::Vec2 delta(activefont->getKerning(previous_charcode,
                        charcode,
                        _kerningType));
#else
                    osg::Vec2 delta(activefont->getKerning(_fontSize,
                        previous_charcode,
                        charcode,
                        _kerningType));
#endif
                  cursor.x() += delta.x() * wr;
                  cursor.y() += delta.y() * hr;
                  break;
                }
                case RIGHT_TO_LEFT:
                {
#if OSG_VERSION_LESS_THAN(3,5,2)
                    osg::Vec2 delta(activefont->getKerning(charcode,
                        previous_charcode,
                        _kerningType));
#else
                    osg::Vec2 delta(activefont->getKerning(_fontSize, charcode,
                        previous_charcode,
                        _kerningType));
#endif
                  cursor.x() -= delta.x() * wr;
                  cursor.y() -= delta.y() * hr;
                  break;
                }
                case VERTICAL:
                  break; // no kerning when vertical.
              }
            }

            local = cursor;
            osg::Vec2 bearing( horizontal ? glyph->getHorizontalBearing()
                                          : glyph->getVerticalBearing() );
            local.x() += bearing.x() * wr;
            local.y() += bearing.y() * hr;

            // set up the coords of the quad
            osg::Vec2 upLeft = local + osg::Vec2(0.f, height);
            osg::Vec2 lowLeft = local;
            osg::Vec2 lowRight = local + osg::Vec2(width, 0.f);
            osg::Vec2 upRight = local + osg::Vec2(width, height);

            // move the cursor onto the next character.
            // also expand bounding box
            switch( _layout )
            {
              case LEFT_TO_RIGHT:
                cursor.x() += glyph->getHorizontalAdvance() * wr;
                bb.expandBy(lowLeft.x(), lowLeft.y());
                bb.expandBy(upRight.x(), upRight.y());
                break;
              case VERTICAL:
                cursor.y() -= glyph->getVerticalAdvance() * hr;
                bb.expandBy(upLeft.x(), upLeft.y());
                bb.expandBy(lowRight.x(), lowRight.y());
                break;
              case RIGHT_TO_LEFT:
                bb.expandBy(lowRight.x(), lowRight.y());
                bb.expandBy(upLeft.x(), upLeft.y());
                break;
            }
            previous_charcode = charcode;
          }
        }

        // skip over spaces and return.
        while( itr != text.end() && *itr == ' ' )
          ++itr;
        if( itr != text.end() && *itr == '\n' )
          ++itr;
      }
      else
      {
        ++itr;
      }

      // move to new line.
      switch( _layout )
      {
        case LEFT_TO_RIGHT:
        {
          startOfLine_coords.y() -= _characterHeight * (1.0 + _lineSpacing);
          cursor = startOfLine_coords;
          previous_charcode = 0;
          break;
        }
        case RIGHT_TO_LEFT:
        {
          startOfLine_coords.y() -= _characterHeight * (1.0 + _lineSpacing);
          cursor = startOfLine_coords;
          previous_charcode = 0;
          break;
        }
        case VERTICAL:
        {
          startOfLine_coords.x() += _characterHeight * (1.0 + _lineSpacing)
                                  / getCharacterAspectRatio();
          cursor = startOfLine_coords;
          previous_charcode = 0;
          break;
        }
      }
    }

    const_cast<TextOSG*>(this)->_maximumWidth = max_width_safe;

    return bb.size();
  }

  //----------------------------------------------------------------------------
  osg::BoundingBox
#if OSG_VERSION_LESS_THAN(3,3,2)
  Text::TextOSG::computeBound()
#else
  Text::TextOSG::computeBoundingBox()
#endif
  const
  {
    osg::BoundingBox bb =
#if OSG_VERSION_LESS_THAN(3,3,2)
      osgText::Text::computeBound();
#else
      osgText::Text::computeBoundingBox();
#endif

#if OSG_VERSION_LESS_THAN(3,1,0)
    if( bb.valid() )
    {
      // TODO bounding box still doesn't seem always right (eg. with center
      //      horizontal alignment not completely accurate)
      bb._min.y() += _offset.y();
      bb._max.y() += _offset.y();
    }
#endif

    return bb;
  }

#if OSG_VERSION_LESS_THAN(3,5,6)
  void Text::TextOSG::computePositions(unsigned int contextID) const
  {
    if( _textureGlyphQuadMap.empty() || _layout == VERTICAL )
      return osgText::Text::computePositions(contextID);

    // TODO check when it can be larger
    assert( _textureGlyphQuadMap.size() == 1 );

    const GlyphQuads& quads = _textureGlyphQuadMap.begin()->second;
    const GlyphQuads::Glyphs& glyphs = quads._glyphs;
#if OSG_VERSION_LESS_THAN(3,3,5)
    GlyphQuads::Coords2 const& coords = quads._coords;
#else
    GlyphQuads::Coords2 refCoords = quads._coords;
    GlyphQuads::Coords2::element_type &coords = *refCoords.get();
#endif

    const GlyphQuads::LineNumbers& line_numbers = quads._lineNumbers;

    float wr = _characterHeight / getCharacterAspectRatio();

    size_t cur_line = static_cast<size_t>(-1);
    for(size_t i = 0; i < glyphs.size(); ++i)
    {
      // Check horizontal offsets

      bool first_char = cur_line != line_numbers[i];
      cur_line = line_numbers[i];

      bool last_char = (i + 1 == glyphs.size())
                    || (cur_line != line_numbers[i + 1]);

      if( first_char || last_char )
      {
        // From osg/src/osgText/Text.cpp:
        //
        // osg::Vec2 upLeft = local+osg::Vec2(0.0f-fHorizQuadMargin, ...);
        // osg::Vec2 lowLeft = local+osg::Vec2(0.0f-fHorizQuadMargin, ...);
        // osg::Vec2 lowRight = local+osg::Vec2(width+fHorizQuadMargin, ...);
        // osg::Vec2 upRight = local+osg::Vec2(width+fHorizQuadMargin, ...);

        float left = coords[i * 4].x(),
              right = coords[i * 4 + 2].x(),
              width = glyphs[i]->getWidth() * wr;

        // (local + width + fHoriz) - (local - fHoriz) = width + 2*fHoriz | -width
        float margin = 0.5f * (right - left - width),
              cursor_x = left + margin
                       - glyphs[i]->getHorizontalBearing().x() * wr;

        if( first_char )
        {
          if( cur_line == 0 || cursor_x < _textBB._min.x() )
            _textBB._min.x() = cursor_x;
        }

        if( last_char )
        {
          float cursor_w = cursor_x + glyphs[i]->getHorizontalAdvance() * wr;

          if( cur_line == 0 || cursor_w > _textBB._max.x() )
            _textBB._max.x() = cursor_w;
        }
      }
    }

    return osgText::Text::computePositions(contextID);
  }

#else
  void Text::TextOSG::computePositionsImplementation()
  {
          TextBase::computePositionsImplementation();
  }
#endif
 //----------------------------------------------------------------------------

  //----------------------------------------------------------------------------
  const std::string Text::TYPE_NAME = "text";

  //----------------------------------------------------------------------------
  void Text::staticInit()
  {
    if( isInit<Text>() )
      return;

    osg::ref_ptr<TextOSG> Text::*text = &Text::_text;

    addStyle("fill", "color", &TextOSG::setFill, text);
    addStyle("background", "color", &TextOSG::setBackgroundColor, text);
    addStyle("stroke", "color", &TextOSG::setStroke, text);
    addStyle("character-size",
             "numeric",
             static_cast<
               void (TextOSG::*)(float)
             > (&TextOSG::setCharacterSize),
             text);
    addStyle("character-aspect-ratio",
             "numeric",
             &TextOSG::setCharacterAspect, text);
    addStyle("line-height", "numeric", &TextOSG::setLineHeight, text);
    addStyle("font-resolution", "numeric", &TextOSG::setFontResolution, text);
    addStyle("padding", "numeric", &TextOSG::setBoundingBoxMargin, text);
    //  TEXT              = 1 default
    //  BOUNDINGBOX       = 2
    //  FILLEDBOUNDINGBOX = 4
    //  ALIGNMENT         = 8
    addStyle<int>("draw-mode", "", &TextOSG::setDrawMode, text);
    addStyle("max-width", "numeric", &TextOSG::setMaximumWidth, text);
    addStyle("font", "", &Text::setFont);
    addStyle("alignment", "", &Text::setAlignment);
    addStyle("text", "", &Text::setText, false);

    osgDB::Registry* reg = osgDB::Registry::instance();
    if( !reg->getReaderWriterForExtension("ttf") )
      SG_LOG(SG_GL, SG_ALERT, "canvas::Text: Missing 'ttf' font reader");
  }

  //----------------------------------------------------------------------------
  Text::Text( const CanvasWeakPtr& canvas,
              const SGPropertyNode_ptr& node,
              const Style& parent_style,
              ElementWeakPtr parent ):
    Element(canvas, node, parent_style, parent),
    _text( new Text::TextOSG(this) )
  {
    staticInit();

    setDrawable(_text);
    _text->setCharacterSizeMode(osgText::Text::OBJECT_COORDS);
    _text->setAxisAlignment(osgText::Text::USER_DEFINED_ROTATION);
    _text->setRotation(osg::Quat(osg::PI, osg::X_AXIS));

    setupStyle();
  }

  //----------------------------------------------------------------------------
  Text::~Text()
  {

  }

  //----------------------------------------------------------------------------
  void Text::setText(const char* text)
  {
    _text->setText(text, osgText::String::ENCODING_UTF8);
  }

  //----------------------------------------------------------------------------
  void Text::setFont(const char* name)
  {
    _text->setFont( Canvas::getSystemAdapter()->getFont(name) );
  }

  //----------------------------------------------------------------------------
  void Text::setAlignment(const char* align)
  {
    const std::string align_string(align);
    if( 0 ) return;
#define ENUM_MAPPING(enum_val, string_val) \
    else if( align_string == string_val )\
      _text->setAlignment( osgText::Text::enum_val );
#include "text-alignment.hxx"
#undef ENUM_MAPPING
    else
    {
      if( !align_string.empty() )
        SG_LOG
        (
          SG_GENERAL,
          SG_WARN,
          "canvas::Text: unknown alignment '" << align_string << "'"
        );
      _text->setAlignment(osgText::Text::LEFT_BASE_LINE);
    }
  }

  //----------------------------------------------------------------------------
#if 0
  const char* Text::getAlignment() const
  {
    switch( _text->getAlignment() )
    {
#define ENUM_MAPPING(enum_val, string_val) \
      case osgText::Text::enum_val:\
        return string_val;
#include "text-alignment.hxx"
#undef ENUM_MAPPING
      default:
        return "unknown";
    }
  }
#endif

  //----------------------------------------------------------------------------
  int Text::heightForWidth(int w) const
  {
    return _text->sizeForWidth(w).y();
  }

  //----------------------------------------------------------------------------
  int Text::maxWidth() const
  {
    return _text->sizeForWidth(INT_MAX).x();
  }

  //----------------------------------------------------------------------------
  size_t Text::lineCount() const
  {
    return _text->lineCount();
  }

  //----------------------------------------------------------------------------
  size_t Text::lineLength(size_t line) const
  {
    return _text->lineAt(line).size();
  }

  //----------------------------------------------------------------------------
  osg::Vec2 Text::getNearestCursor(const osg::Vec2& pos) const
  {
    return _text->nearestLine(pos.y()).nearestCursor(pos.x());
  }

  //----------------------------------------------------------------------------
  osg::Vec2 Text::getCursorPos(size_t line, size_t character) const
  {
    return _text->lineAt(line).cursorPos(character);
  }

  //----------------------------------------------------------------------------
  osg::StateSet* Text::getOrCreateStateSet()
  {
    if( !_scene_group.valid() )
      return nullptr;

    // Only check for StateSet on Transform, as the text stateset is shared
    // between all text instances using the same font (texture).
    return _scene_group->getOrCreateStateSet();
  }

} // namespace canvas
} // namespace simgear
