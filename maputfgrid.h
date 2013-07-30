/******************************************************************************
 * $Id$
 *
 * Project:  MapServer
 * Purpose:  UTFGrid rendering functions (using AGG primitives)
 * Author:   Francois Desjarlais
 *
 ******************************************************************************
 * Copyright (c) 1996-2007 Regents of the University of Minnesota.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies of this Software or works derived from this Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "renderers/agg/include/agg_renderer_base.h"
#include "renderers/agg/include/agg_rendering_buffer.h"

/*
 * Using AGG templates to create UTFGrid pixel. This pixel could also be used
 * as a gray pixel using int32u. The only difference is that color blender is
 * disabled.
 *
 * Reference : agg_pixfmt_gray.h
 */

//==================================================================utfpix32
struct utfpix32
{
  typedef mapserver::int32u value_type;
  typedef mapserver::int64u calc_type;
  typedef mapserver::int64  long_type;

  enum base_scale_e
  {
    base_shift = 16,
    base_scale = 1 << base_shift,
    base_mask  = base_scale - 1
  };
  typedef utfpix32 self_type;

  value_type v;
  // value_type a;

  //--------------------------------------------------------------------
  utfpix32() {}

  //--------------------------------------------------------------------
  utfpix32(unsigned v_, unsigned a_=base_mask) :
    v(mapserver::int32u(v_)), a(mapserver::int32u(a_)) {}

  //--------------------------------------------------------------------
  utfpix32(const self_type& c, unsigned a_) :
    v(c.v), a(value_type(a_)) {}

  //--------------------------------------------------------------------
  void clear()
  {
    v = a = 0;
  }

  //--------------------------------------------------------------------
  const self_type& transparent()
  {
    a = 0;
    return *this;
  }

  //--------------------------------------------------------------------
  void opacity(double a_)
  {
    // if(a_ < 0.0) a_ = 0.0;
    a_ = 1.0;
    // a = (value_type)mapserver::uround(a_ * double(base_mask));
  }

  //--------------------------------------------------------------------
  double opacity() const
  {
    return 1;
  }

  //--------------------------------------------------------------------
  const self_type& premultiply()
  {
    // if(a == base_mask) return *this;
    if(a == 0) 
    {
      v = 0;
      return *this;
    }
    v = value_type((calc_type(v) * a) >> base_shift);
    return *this;
  }

  //--------------------------------------------------------------------
  const self_type& premultiply(unsigned a_)
  {
    // if(a == base_mask && a_ >= base_mask) return *this;
    if(a == 0 || a_ == 0) 
    {
      v = a = 0;
      return *this;
    }
    calc_type v_ = (calc_type(v) * a_) / a;
    v = value_type((v_ > a_) ? a_ : v_);
    a = value_type(a_);
    return *this;
  }

  //--------------------------------------------------------------------
  const self_type& demultiply()
  {
    // if(a == base_mask) return *this;
    if(a == 0) 
    {
      v = 0;
      return *this;
    }
    // calc_type v_ = (calc_type(v) * base_mask) / a;
    // v = value_type((v_ > base_mask) ? base_mask : v_);
    return *this;
  }

  //--------------------------------------------------------------------
  self_type gradient(self_type c, double k) const
  {
    self_type ret;
    calc_type ik = mapserver::uround(k * base_scale);
    ret.v = value_type(calc_type(v) + (((calc_type(c.v) - v) * ik) >> base_shift));
    // ret.a = value_type(calc_type(a) + (((calc_type(c.a) - a) * ik) >> base_shift));
    return ret;
  }

  //--------------------------------------------------------------------
  AGG_INLINE void add(const self_type& c, unsigned cover)
  {
    calc_type cv, ca;
    if(cover == mapserver::cover_mask) 
    {
      // if(c.a == base_mask)
      // {
        // *this = c;
      // }
      // else 
      // {
        // cv = v + c.v; v = (cv > calc_type(base_mask)) ? calc_type(base_mask) : cv;
        // ca = a + c.a; a = (ca > calc_type(base_mask)) ? calc_type(base_mask) : ca;
      // }
    }
    else 
    {
      cv = v + ((c.v * cover + mapserver::cover_mask/2) >> mapserver::cover_shift);
      // ca = a + ((c.a * cover + mapserver::cover_mask/2) >> mapserver::cover_shift);
      // v = (cv > calc_type(base_mask)) ? calc_type(base_mask) : cv;
      // a = (ca > calc_type(base_mask)) ? calc_type(base_mask) : ca;
    }
  }

  static self_type no_color() { return self_type(0,0); }
};

//=================================================pixfmt_alpha_blend_utf
template<class ColorT, class RenBuf, unsigned Step=1, unsigned Offset=0>
class pixfmt_alpha_blend_utf
{
public:
  typedef RenBuf   rbuf_type;
  typedef typename rbuf_type::row_data row_data;
  typedef ColorT                            color_type;
  typedef typename color_type::value_type   value_type;
  typedef typename color_type::calc_type    calc_type;
  typedef int                               order_type; // A fake one
  enum base_scale_e
  {
    base_shift = color_type::base_shift,
    base_scale = color_type::base_scale,
    // base_mask  = color_type::base_mask,
    pix_width  = sizeof(value_type),
    pix_step   = Step,
    pix_offset = Offset
  };

private:
  //--------------------------------------------------------------------
  static AGG_INLINE void copy_or_blend_pix(value_type* p, 
                                           const color_type& c, 
                                           unsigned cover)
  {
    // if (c.a) 
    // {
      // calc_type alpha = (calc_type(c.a) * (cover + 1)) >> 8;
      *p = c.v;
    // }
  }

  static AGG_INLINE void copy_or_blend_pix(value_type* p, 
                                           const color_type& c)
  {
    // if (c.a) 
    // {
      *p = c.v;
    // }
  }

public:
  pixfmt_alpha_blend_utf() : m_rbuf(0) {}
  //--------------------------------------------------------------------
  explicit pixfmt_alpha_blend_utf(rbuf_type& rb) :
    m_rbuf(&rb)
  {}
  void attach(rbuf_type& rb) { m_rbuf = &rb; }
  //--------------------------------------------------------------------

  template<class PixFmt>
  bool attach(PixFmt& pixf, int x1, int y1, int x2, int y2)
  {
    mapserver::rect_i r(x1, y1, x2, y2);
    if(r.clip(mapserver::rect_i(0, 0, pixf.width()-1, pixf.height()-1)))
    {
      int stride = pixf.stride();
      m_rbuf->attach(pixf.pix_ptr(r.x1, stride < 0 ? r.y2 : r.y1), 
                     (r.x2 - r.x1) + 1,
                     (r.y2 - r.y1) + 1, 
                     stride);
      return true;
    }
    return false;
  }

  //--------------------------------------------------------------------
  AGG_INLINE unsigned width()  const { return m_rbuf->width();  }
  AGG_INLINE unsigned height() const { return m_rbuf->height(); }
  AGG_INLINE int      stride() const { return m_rbuf->stride(); }

  //--------------------------------------------------------------------
        mapserver::int8u* row_ptr(int y)       { return m_rbuf->row_ptr(y); }
  const mapserver::int8u* row_ptr(int y) const { return m_rbuf->row_ptr(y); }
  row_data                row(int y)     const { return m_rbuf->row(y); }

  const mapserver::int8u* pix_ptr(int x, int y) const
  {
    return m_rbuf->row_ptr(y) + x * Step + Offset;
  }

  mapserver::int8u* pix_ptr(int x, int y)
  {
    return m_rbuf->row_ptr(y) + x * Step + Offset;
  }

  //--------------------------------------------------------------------
  AGG_INLINE static void make_pix(mapserver::int8u* p, const color_type& c)
  {
    *(value_type*)p = c.v;
  }

  //--------------------------------------------------------------------
  AGG_INLINE color_type pixel(int x, int y) const
  {
    value_type* p = (value_type*)m_rbuf->row_ptr(y) + x * Step + Offset;
    return color_type(*p);
  }

  //--------------------------------------------------------------------
  AGG_INLINE void copy_pixel(int x, int y, const color_type& c)
  {
    *((value_type*)m_rbuf->row_ptr(x, y, 1) + x * Step + Offset) = c.v;
  }

  //--------------------------------------------------------------------
  AGG_INLINE void blend_pixel(int x, int y, const color_type& c, mapserver::int8u cover)
  {
    copy_or_blend_pix((value_type*) 
                       m_rbuf->row_ptr(x, y, 1) + x * Step + Offset, 
                       c, 
                       cover);
  }


  //--------------------------------------------------------------------
  AGG_INLINE void copy_hline(int x, int y, 
                             unsigned len, 
                             const color_type& c)
  {
    value_type* p = (value_type*)
        m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

    do
    {
      *p = c.v;
      p += Step;
    }
    while(--len);
  }


  //--------------------------------------------------------------------
  AGG_INLINE void copy_vline(int x, int y, 
                             unsigned len, 
                             const color_type& c)
  {
    do
    {
      value_type* p = (value_type*) 
          m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

      *p = c.v;
    }
    while(--len);
  }


  //--------------------------------------------------------------------
  void blend_hline(int x, int y, 
                   unsigned len, 
                   const color_type& c, 
                   mapserver::int8u cover)
  {
    value_type* p = (value_type*) 
        m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

    do
    {
      *p = c.v;
      p += Step;
    }
    while(--len);
  }


  //--------------------------------------------------------------------
  void blend_vline(int x, int y, 
                   unsigned len, 
                   const color_type& c, 
                   mapserver::int8u cover)
  {
    do
    {
      value_type* p = (value_type*) 
          m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

      *p = c.v;
    }
    while(--len);
  }


  //--------------------------------------------------------------------
  void blend_solid_hspan(int x, int y, 
                         unsigned len, 
                         const color_type& c, 
                         const mapserver::int8u* covers)
  {
    // if (c.a) 
    // {
      value_type* p = (value_type*) 
          m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

      do
      {
        // calc_type alpha = (calc_type(c.a) * (calc_type(*covers) + 1)) >> 8;
        *p = c.v;
        p += Step;
        ++covers;
      }
      while(--len);
    // }
  }


  //--------------------------------------------------------------------
  void blend_solid_vspan(int x, int y, 
                         unsigned len, 
                         const color_type& c, 
                         const mapserver::int8u* covers)
  {
    // if (c.a) 
    // {
      do
      {
        // calc_type alpha = (calc_type(c.a) * (calc_type(*covers) + 1)) >> 8;

        value_type* p = (value_type*) 
            m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

        *p = c.v;
        ++covers;
      }
      while(--len);
    // }
  }


  //--------------------------------------------------------------------
  void copy_color_hspan(int x, int y, 
                        unsigned len, 
                        const color_type* colors)
  {
    value_type* p = (value_type*) 
        m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

    do
    {
      *p = colors->v;
      p += Step;
      ++colors;
    }
    while(--len);
  }


  //--------------------------------------------------------------------
  void copy_color_vspan(int x, int y, 
                        unsigned len, 
                        const color_type* colors)
  {
    do
    {
      value_type* p = (value_type*) 
          m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;
      *p = colors->v;
      ++colors;
    }
    while(--len);
  }


  //--------------------------------------------------------------------
  void blend_color_hspan(int x, int y, 
                         unsigned len, 
                         const color_type* colors, 
                         const mapserver::int8u* covers, 
                         mapserver::int8u cover)
  {
    value_type* p = (value_type*) 
        m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

    if(covers) 
    {
      do
      {
        copy_or_blend_pix(p, *colors++, *covers++);
        p += Step;
      }
      while(--len);
    } 
    else 
    {
      if(cover == 255) {
        do
        {
          // if(colors->a == base_mask)
          // {
          //   *p = colors->v;
          // }
          // else
          // {
            copy_or_blend_pix(p, *colors);
          // }
          p += Step;
          ++colors;
        }
        while(--len);
      } 
      else 
      {
        do
        {
          copy_or_blend_pix(p, *colors++, cover);
          p += Step;
        }
        while(--len);
      }
    }
  }


  //--------------------------------------------------------------------
  void blend_color_vspan(int x, int y, 
                         unsigned len, 
                         const color_type* colors, 
                         const mapserver::int8u* covers, 
                         mapserver::int8u cover)
  {
    value_type* p;
    if(covers) 
    {
      do
      {
        p = (value_type*) 
            m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

        copy_or_blend_pix(p, *colors++, *covers++);
      }
      while(--len);
    } 
    else 
    {
      if(cover == 255) 
      {
        do
        {
          p = (value_type*) 
              m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

          // if(colors->a == base_mask)
          // {
          //   *p = colors->v;
          // }
          // else
          // {
            copy_or_blend_pix(p, *colors);
          // }
          ++colors;
        }
        while(--len);
      } 
      else 
      {
        do
        {
          p = (value_type*) 
              m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

          copy_or_blend_pix(p, *colors++, cover);
        }
        while(--len);
      }
    }
  }

private:
  rbuf_type* m_rbuf;
};



