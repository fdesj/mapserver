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
/*
 * Je comprends pas encore sa
 *
 */

struct utfpix32
{
  typedef mapserver::int32 value_type;
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
  value_type a;

  utfpix32() {}

  utfpix32(value_type v_, unsigned a_=base_mask) :
    v(v_), a(value_type(a_)) {}

  utfpix32(const self_type& c, unsigned a_) :
    v(c.v), a(value_type(a_)) {}

  void clear()
  {
    v = a = 0;
  }

  const self_type& transparent()
  {
    a = 0;
    return *this;
  }

  void opacity(double a_)
  {
    if(a_ < 0.0) 
    	a_ = 0.0;
    if(a_ > 1.0) 
    	a_ = 1.0;
    a = (value_type)mapserver::uround(a_ * double(base_mask));
  }

  double opacity() const
  {
    return double(a) / double(base_mask);
  }

  const self_type& premultiply()
  {
    if(a == base_mask) 
    	return *this;
    if(a == 0) {
      v = 0;
      return *this;
    }
    v = value_type((calc_type(v) * a) >> base_shift);
    return *this;
  }

  const self_type& premultiply(unsigned a_)
  {
    if(a == base_mask && a_ >= base_mask) 
      return *this;
    if(a == 0 || a_ == 0) {
      v = a = 0;
      return *this;
    }
    calc_type v_ = (calc_type(v) * a_) / a;
    v = value_type((v_ > a_) ? a_ : v_);
    a = value_type(a_);
    return *this;
  }

  const self_type& demultiply()
  {
    if(a == base_mask) 
      return *this;
    if(a == 0) {
      v = 0;
      return *this;
    }
    calc_type v_ = (calc_type(v) * base_mask) / a;
    v = value_type((v_ > base_mask) ? base_mask : v_);
    return *this;
  }

  self_type gradient(self_type c, double k) const
  {
    self_type ret;
    calc_type ik = mapserver::uround(k * base_scale);
    ret.v = value_type(calc_type(v) + (((calc_type(c.v) - v) * ik) >> base_shift));
    ret.a = value_type(calc_type(a) + (((calc_type(c.a) - a) * ik) >> base_shift));
    return ret;
  }

  AGG_INLINE void add(const self_type& c, unsigned cover)
  {
    calc_type cv, ca;
    if(cover == mapserver::cover_mask) {
      if(c.a == base_mask)
        *this = c;
      else {
        cv = v + c.v; v = (cv > calc_type(base_mask)) ? calc_type(base_mask) : cv;
        ca = a + c.a; a = (ca > calc_type(base_mask)) ? calc_type(base_mask) : ca;
      }
    }
    else {
      cv = v + ((c.v * cover + mapserver::cover_mask/2) >> mapserver::cover_shift);
      ca = a + ((c.a * cover + mapserver::cover_mask/2) >> mapserver::cover_shift);
      v = (cv > calc_type(base_mask)) ? calc_type(base_mask) : cv;
      a = (ca > calc_type(base_mask)) ? calc_type(base_mask) : ca;
    }
  }

  static self_type no_color() 
  { 
    return self_type(0,0); 
  }
};

/*
 * Je comprends pas encore sa
 *
 */

template<class ColorT> struct blender_utf
{
  typedef ColorT color_type;
  typedef typename color_type::value_type value_type;
  typedef typename color_type::calc_type calc_type;
  enum base_scale_e { base_shift = color_type::base_shift };

  static AGG_INLINE void blend_pix(value_type* p, unsigned cv, unsigned alpha, unsigned cover=0)
  {
  *p = (value_type)((((cv - calc_type(*p)) * alpha) + (calc_type(*p) << base_shift)) >> base_shift);
  }
};

template<class ColorT, class GammaLut> class apply_gamma_dir_utf
{
  public:
    typedef typename ColorT::value_type value_type;

    apply_gamma_dir_utf(const GammaLut& gamma) : m_gamma(gamma) {}

    AGG_INLINE void operator () (value_type* p)
    {
      *p = m_gamma.dir(*p);
    }

  private:
    const GammaLut& m_gamma;
};

template<class ColorT, class GammaLut> class apply_gamma_inv_utf
{
  public:
    typedef typename ColorT::value_type value_type;

    apply_gamma_inv_utf(const GammaLut& gamma) : m_gamma(gamma) {}

    AGG_INLINE void operator () (value_type* p)
    {
      *p = m_gamma.inv(*p);
    }

  private:
    const GammaLut& m_gamma;
};

template<class Blender, class RenBuf, unsigned Step=1, unsigned Offset=0>
class pixfmt_alpha_blend_utf
{
  public:
    typedef RenBuf rbuf_type;
    typedef typename rbuf_type::row_data row_data;
    typedef Blender blender_type;
    typedef typename blender_type::color_type color_type;
    typedef int order_type; // A fake one
    typedef typename color_type::value_type value_type;
    typedef typename color_type::calc_type calc_type;
    enum base_scale_e
    {
      base_shift = color_type::base_shift,
      base_scale = color_type::base_scale,
      base_mask  = color_type::base_mask,
      pix_width  = sizeof(value_type),
      pix_step   = Step,
      pix_offset = Offset
    };

  private:
    static AGG_INLINE void copy_or_blend_pix(value_type* p, const color_type& c, unsigned cover)
    {
      if (c.a) {
        calc_type alpha = (calc_type(c.a) * (cover + 1)) >> 8;
        if(alpha == base_mask)
          *p = c.v;
        else
          Blender::blend_pix(p, c.v, alpha, cover);
      }
    }

    static AGG_INLINE void copy_or_blend_pix(value_type* p, const color_type& c)
    {
      if (c.a) {
        if(c.a == base_mask)
          *p = c.v;
        else
          Blender::blend_pix(p, c.v, c.a);
      }
    }

  public:
    explicit pixfmt_alpha_blend_utf(rbuf_type& rb) :
      m_rbuf(&rb)
    {}
    void attach(rbuf_type& rb) { m_rbuf = &rb; }

    template<class PixFmt>
    bool attach(PixFmt& pixf, int x1, int y1, int x2, int y2)
    {
      mapserver::rect_i r(x1, y1, x2, y2);
      if(r.clip(mapserver::rect_i(0, 0, pixf.width()-1, pixf.height()-1)))
      {
        int stride = pixf.stride();
        m_rbuf->attach(pixf.pix_ptr(r.x1, stride < 0 ? r.y2 : r.y1), (r.x2 - r.x1) + 1, (r.y2 - r.y1) + 1, stride);
        return true;
      }
      return false;
    }

    AGG_INLINE unsigned width()  const 
    { 
      return m_rbuf->width();
    }

    AGG_INLINE unsigned height() const 
    { 
      return m_rbuf->height();
    }

    AGG_INLINE int stride() const 
    { 
      return m_rbuf->stride(); 
    }

    mapserver::int8u* row_ptr(int y)       
    { 
      return m_rbuf->row_ptr(y); 
    }

    const mapserver::int8u* row_ptr(int y) const 
    { 
      return m_rbuf->row_ptr(y); 
    }

    row_data row(int y) const 
    { 
      return m_rbuf->row(y); 
    }

    const mapserver::int8u* pix_ptr(int x, int y) const
    {
      return m_rbuf->row_ptr(y) + x * Step + Offset;
    }

    mapserver::int8u* pix_ptr(int x, int y)
    {
      return m_rbuf->row_ptr(y) + x * Step + Offset;
    }

    AGG_INLINE static void make_pix(mapserver::int8u* p, const color_type& c)
    {
      *(value_type*)p = c.v;
    }

    AGG_INLINE color_type pixel(int x, int y) const
    {
      value_type* p = (value_type*)m_rbuf->row_ptr(y) + x * Step + Offset;
      return color_type(*p);
    }

    AGG_INLINE void copy_pixel(int x, int y, const color_type& c)
    {
      *((value_type*)m_rbuf->row_ptr(x, y, 1) + x * Step + Offset) = c.v;
    }

    AGG_INLINE void blend_pixel(int x, int y, const color_type& c, mapserver::int8u cover)
    {
      copy_or_blend_pix((value_type*) m_rbuf->row_ptr(x, y, 1) + x * Step + Offset, c, cover);
    }

    AGG_INLINE void copy_hline(int x, int y, unsigned len, const color_type& c)
    {
      value_type* p = (value_type*) m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

      do
      {
        *p = c.v;
        p += Step;
      }
      while(--len);
    }

    AGG_INLINE void copy_vline(int x, int y, unsigned len, const color_type& c)
    {
      do
      {
        value_type* p = (value_type*) m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

        *p = c.v;
      }
      while(--len);
    }

    void blend_hline(int x, int y, unsigned len, const color_type& c, mapserver::int8u cover)
    {
      value_type* p = (value_type*) m_rbuf->row_ptr(x, y, len) + x * Step + Offset;
      do
      {
        *p = c.v;
        p += Step;
      }
      while(--len);
      // We ignore alpha since grid_renderer is a binary renderer
      /*if (c.a)
      {
        value_type* p = (value_type*)
        m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

        calc_type alpha = (calc_type(c.a) * (cover + 1)) >> 8;
        if(alpha == base_mask) {
          do
          {
            *p = c.v;
            p += Step;
          }
          while(--len);
        } else {
          do
          {
            Blender::blend_pix(p, c.v, alpha, cover);
            p += Step;
          }
          while(--len);
        }
      }*/
    }

    void blend_vline(int x, int y, unsigned len, const color_type& c, mapserver::int8u cover)
    {
      if (c.a) {
        value_type* p;
        calc_type alpha = (calc_type(c.a) * (cover + 1)) >> 8;
        if(alpha == base_mask) {
          do
          {
            p = (value_type*) m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

            *p = c.v;
          }
          while(--len);
        } else {
          do
          {
              p = (value_type*) m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

              Blender::blend_pix(p, c.v, alpha, cover);
          }
          while(--len);
        }
      }
    }

    void blend_solid_hspan(int x, int y, unsigned len, const color_type& c, const mapserver::int8u* covers)
    {
      if (c.a) {
        value_type* p = (value_type*) m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

        do
        {
          calc_type alpha = (calc_type(c.a) * (calc_type(*covers) + 1)) >> 8;
          if(alpha == base_mask) {
            *p = c.v;
          } else {
            Blender::blend_pix(p, c.v, alpha, *covers);
          }
          p += Step;
          ++covers;
        }
        while(--len);
      }
    }

    void blend_solid_vspan(int x, int y, unsigned len, const color_type& c, const mapserver::int8u* covers)
    {
      if (c.a) {
        do
        {
          calc_type alpha = (calc_type(c.a) * (calc_type(*covers) + 1)) >> 8;

          value_type* p = (value_type*) m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

          if(alpha == base_mask) {
            *p = c.v;
          } else {
                Blender::blend_pix(p, c.v, alpha, *covers);
            }
          ++covers;
        }
        while(--len);
      }
    }

    void copy_color_hspan(int x, int y, unsigned len, const color_type* colors)
    {
      value_type* p = (value_type*) m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

      do
      {
        *p = colors->v;
        p += Step;
        ++colors;
      }
      while(--len);
    }

    void copy_color_vspan(int x, int y, unsigned len, const color_type* colors)
    {
      do
      {
        value_type* p = (value_type*) m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;
        *p = colors->v;
        ++colors;
      }
      while(--len);
    }

    void blend_color_hspan(int x, int y, unsigned len, const color_type* colors, const mapserver::int8u* covers, mapserver::int8u cover)
    {
      value_type* p = (value_type*) m_rbuf->row_ptr(x, y, len) + x * Step + Offset;

      if(covers) {
            do
            {
              copy_or_blend_pix(p, *colors++, *covers++);
              p += Step;
            }
            while(--len);
      } else {
        if(cover == 255) {
          do
          {
            if(colors->a == base_mask)
              *p = colors->v;
            else
              copy_or_blend_pix(p, *colors);
            p += Step;
            ++colors;
          }
          while(--len);
        } else {
          do
          {
            copy_or_blend_pix(p, *colors++, cover);
            p += Step;
          }
          while(--len);
        }
      }
    }

    void blend_color_vspan(int x, int y, unsigned len, const color_type* colors, const mapserver::int8u* covers, mapserver::int8u cover)
    {
      value_type* p;
      if(covers) {
        do
        {
          p = (value_type*) m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

          copy_or_blend_pix(p, *colors++, *covers++);
        }
        while(--len);
      } else {
        if(cover == 255) {
          do
          {
            p = (value_type*) m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

            if(colors->a == base_mask)
              *p = colors->v;
            else
              copy_or_blend_pix(p, *colors);
            ++colors;
          }
          while(--len);
        } else {
          do
          {
            p = (value_type*) m_rbuf->row_ptr(x, y++, 1) + x * Step + Offset;

            copy_or_blend_pix(p, *colors++, cover);
          }
          while(--len);
        }
      }
    }

    template<class Function> void for_each_pixel(Function f)
    {
      unsigned y;
      for(y = 0; y < height(); ++y)
      {
        row_data r = m_rbuf->row(y);
        if(r.ptr) {
          unsigned len = r.x2 - r.x1 + 1;

          value_type* p = (value_type*) m_rbuf->row_ptr(r.x1, y, len) + r.x1 * Step + Offset;

          do
          {
            f(p);
            p += Step;
          }
          while(--len);
        }
      }
    }

    template<class GammaLut> void apply_gamma_dir(const GammaLut& g)
    {
      for_each_pixel(apply_gamma_dir_utf<color_type, GammaLut>(g));
    }

    template<class GammaLut> void apply_gamma_inv(const GammaLut& g)
    {
      for_each_pixel(apply_gamma_inv_utf<color_type, GammaLut>(g));
    }

    template<class RenBuf2>
    void copy_from(const RenBuf2& from, int xdst, int ydst, int xsrc, int ysrc, unsigned len)
    {
      const mapserver::int8u* p = from.row_ptr(ysrc);
      if(p)
        memmove(m_rbuf->row_ptr(xdst, ydst, len) + xdst * pix_width, p + xsrc * pix_width, len * pix_width);
    }

    template<class SrcPixelFormatRenderer>
    void blend_from_color(const SrcPixelFormatRenderer& from, const color_type& color, int xdst, int ydst, int xsrc, int ysrc, unsigned len, mapserver::int8u cover)
    {
      typedef typename SrcPixelFormatRenderer::value_type src_value_type;
      const src_value_type* psrc = (src_value_type*)from.row_ptr(ysrc);
      if(psrc) {
        value_type* pdst = (value_type*)m_rbuf->row_ptr(xdst, ydst, len) + xdst;
        do
        {
          copy_or_blend_pix(pdst, color, (*psrc * cover + base_mask) >> base_shift);
          ++psrc;
          ++pdst;
        }
        while(--len);
      }
    }

    template<class SrcPixelFormatRenderer>
    void blend_from_lut(const SrcPixelFormatRenderer& from, const color_type* color_lut, int xdst, int ydst, int xsrc, int ysrc, unsigned len, mapserver::int8u cover)
    {
      typedef typename SrcPixelFormatRenderer::value_type src_value_type;
      const src_value_type* psrc = (src_value_type*)from.row_ptr(ysrc);
      if(psrc) {
        value_type* pdst = (value_type*)m_rbuf->row_ptr(xdst, ydst, len) + xdst;
        do
        {
          copy_or_blend_pix(pdst, color_lut[*psrc], cover);
          ++psrc;
          ++pdst;
        }
        while(--len);
      }
    }

  private:
    rbuf_type* m_rbuf;
};



