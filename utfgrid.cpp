/******************************************************************************
 * $Id$
 *
 * Project:  UTFGrid driver
 * Purpose:  UTFGrid rendering for CGI and WMS mode
 * Author:   Francois Desjarlais (fdesjarlais1@gmail.com)
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

#include "mapserver.h"
#include "mapagg.h"
// #include <assert.h>
// #include "renderers/agg/include/agg_color_rgba.h"
#include "renderers/agg/include/agg_pixfmt_rgba.h"
#include "renderers/agg/include/agg_renderer_base.h"
#include "renderers/agg/include/agg_renderer_scanline.h"
// #include "renderers/agg/include/agg_math_stroke.h"
#include "renderers/agg/include/agg_scanline_p.h"
// #include "renderers/agg/include/agg_scanline_u.h"
// #include "renderers/agg/include/agg_rasterizer_scanline_aa.h"
// #include "renderers/agg/include/agg_span_pattern_rgba.h"
// #include "renderers/agg/include/agg_span_allocator.h"
// #include "renderers/agg/include/agg_span_interpolator_linear.h"
// #include "renderers/agg/include/agg_span_image_filter_rgba.h"
// #include "renderers/agg/include/agg_pattern_filters_rgba.h"
// #include "renderers/agg/include/agg_span_image_filter_rgb.h"
// #include "renderers/agg/include/agg_image_accessors.h"
#include "renderers/agg/include/agg_conv_stroke.h"
#include "renderers/agg/include/agg_conv_dash.h"
// #include "renderers/agg/include/agg_path_storage.h"
#include "renderers/agg/include/agg_font_freetype.h"
// #include "renderers/agg/include/agg_conv_contour.h"
// #include "renderers/agg/include/agg_ellipse.h"
#include "renderers/agg/include/agg_gamma_functions.h"

// #include "renderers/agg/include/agg_rasterizer_outline_aa.h"
// #include "renderers/agg/include/agg_renderer_outline_aa.h"
// #include "renderers/agg/include/agg_renderer_outline_image.h"
// #include "renderers/agg/include/agg_span_pattern_rgba.h"
// #include "renderers/agg/include/agg_span_image_filter_rgba.h"
// #include "renderers/agg/include/agg_glyph_raster_bin.h"
// #include "renderers/agg/include/agg_renderer_raster_text.h"
// #include "renderers/agg/include/agg_embedded_raster_fonts.h"

// #include "renderers/agg/include/agg_conv_clipper.h"

#include "renderers/agg/include/agg_renderer_primitives.h"
#include "renderers/agg/include/agg_rasterizer_outline.h"

#include <list> 

typedef mapserver::order_bgra band_order;

typedef mapserver::int8u band_type;
typedef mapserver::rgba8 color_type;
typedef mapserver::pixel32_type pixel_type;

typedef mapserver::blender_rgba_pre<color_type, band_order> blender_pre;

typedef mapserver::pixfmt_alpha_blend_rgba<blender_pre, mapserver::rendering_buffer, pixel_type> pixel_format;
typedef mapserver::rendering_buffer rendering_buffer;
typedef mapserver::renderer_base<pixel_format> renderer_base;
typedef mapserver::renderer_scanline_aa_solid<renderer_base> renderer_scanline;
typedef mapserver::rasterizer_scanline_aa<> rasterizer_scanline;
// typedef mapserver::font_engine_freetype_int16 font_engine_type;
// typedef mapserver::font_cache_manager<font_engine_type> font_manager_type;
// typedef mapserver::conv_curve<font_manager_type::path_adaptor_type> font_curve_type;

typedef mapserver::renderer_primitives<renderer_base> renderer_primitives;
typedef mapserver::rasterizer_outline<renderer_primitives> rasterizer_outline;
static color_type UTFGRID_NO_COLOR = color_type(0, 0, 0, 0);

#define UTFGridItemID(c) mapserver::rgba8_pre(c->red, c->green, c->blue, c->alpha)

typedef struct lookupTable {
  long int table[256];
  int counter;
}
lookupTable;

class UTFGridRenderer
{
public:

  UTFGridRenderer()
    :
    m_renderer_primitives(m_renderer_base),
    m_rasterizer_primitives(m_renderer_primitives)
  {
    stroke = NULL;
    dash = NULL;
    stroke_dash = NULL;
  }

  ~UTFGridRenderer() {
    if(stroke) {
      delete stroke;
    }
    if(dash) {
      delete dash;
    }
    if(stroke_dash) {
      delete stroke_dash;
    }
  }

  band_type* buffer;
  rendering_buffer m_rendering_buffer;
  pixel_format m_pixel_format;
  renderer_base m_renderer_base;
  renderer_scanline m_renderer_scanline;
  renderer_primitives m_renderer_primitives;
  rasterizer_outline m_rasterizer_primitives;
  rasterizer_scanline m_rasterizer_aa_gamma;
  mapserver::scanline_p8 sl_poly;
  mapserver::scanline_u8 sl_line; 
  bool use_alpha;
  mapserver::conv_stroke<line_adaptor> *stroke;
  mapserver::conv_dash<line_adaptor> *dash;
  mapserver::conv_stroke<mapserver::conv_dash<line_adaptor> > *stroke_dash;
  lookupTable lookupTableData;
};


#define UTFGRID_RENDERER(image) ((UTFGridRenderer*) (image)->img.plugin)

/*
 * Generic function to tell the underline device that shape
 * drawing is stating
 */
imageObj *createImageUTFGrid(int width, int height, outputFormatObj *format, colorObj* bg)
{
  imageObj *image = NULL;
  if (format->imagemode != MS_IMAGEMODE_RGB && format->imagemode != MS_IMAGEMODE_RGBA) {
    msSetError(MS_MISCERR,
               "UTFGrid driver only supports RGB or RGBA pixel models.", "createImageUTFGrid()");
    return image;
  }

  image = (imageObj *) calloc(1, sizeof (imageObj));
  MS_CHECK_ALLOC(image, sizeof (imageObj), NULL);
  UTFGridRenderer *r = new UTFGridRenderer();

  r->buffer = (band_type*)malloc(width * height * 4 * sizeof(band_type));
  if (r->buffer == NULL) {
    msSetError(MS_MEMERR, "%s: %d: Out of memory allocating %u bytes.\n", "createImageUTFGrid()",
               __FILE__, __LINE__, width * height * 4 * sizeof(band_type));
    free(image);
    return NULL;
  }

  r->m_rendering_buffer.attach(r->buffer, width, height, width * 4);
  r->m_pixel_format.attach(r->m_rendering_buffer);
  r->m_renderer_base.attach(r->m_pixel_format);
  r->m_renderer_scanline.attach(r->m_renderer_base);
  r->m_rasterizer_aa_gamma.gamma(mapserver::gamma_linear(0.0,0.0));
  r->m_renderer_base.clear(UTFGRID_NO_COLOR);
  r->use_alpha = false;
  image->img.plugin = (void*) r;
  format->vtable->renderer_data = (void*) &r->lookupTableData;
  r->lookupTableData.counter=0;

  return image;
}

/*
 * Generic function to tell the underline device that shape
 * drawing is stating
 */
int saveImageUTFGrid(imageObj *img, mapObj *map, FILE *fp, outputFormatObj *format)
{
  msSetError(MS_MISCERR, "UTFGrid does not support direct image saving", "saveImageUTFGrid()");

  return MS_FAILURE;
}

/*
 * Render polygon shapes with UTFGrid
 *
 */
int renderPolygonUTFGrid(imageObj *img, shapeObj *p, colorObj *color)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);
  colorObj *index = NULL;
  index = (colorObj *) calloc(1, sizeof (colorObj));
  polygon_adaptor polygons(p);
  r->m_rasterizer_aa_gamma.reset();
  r->m_rasterizer_aa_gamma.filling_rule(mapserver::fill_even_odd);
  r->m_rasterizer_aa_gamma.add_path(polygons);
  // Splitting the long int from the shpaeobj into 4 char. Like this
  // we can use this to retrieve index with the color. We're not using the alpha byte 
  // since AGG does a blend and our color get changed.
  r->lookupTableData.table[r->lookupTableData.counter]=p->index;
  color->red = r->lookupTableData.counter & 0x000000ff;
  color->green = ((r->lookupTableData.counter & 0x0000ff00) / 0x100);
  color->blue = ((r->lookupTableData.counter & 0x00ff0000) / 0x10000);
  r->lookupTableData.counter++;
  r->m_renderer_scanline.color(UTFGridItemID(color));
  mapserver::render_scanlines(r->m_rasterizer_aa_gamma, r->sl_poly, r->m_renderer_scanline);
  return MS_SUCCESS;
}

/*
 * Return success to avoid error message.
 *
 */
int renderLineUTFGrid(imageObj *img, shapeObj *p, strokeStyleObj *stroke)
{
  // UTFGridRenderer *r = UTFGRID_RENDERER(img);
  // line_adaptor lines = line_adaptor(p);

  // r->m_renderer_primitives.line_color(UTFGridItemID(stroke->color));
  // r->m_rasterizer_primitives.add_path(lines);
  return MS_SUCCESS;
}

int utfgridInitializeRasterBuffer(rasterBufferObj *rb, int width, int height, int mode)
{
  rb->type = MS_BUFFER_BYTE_RGBA;
  rb->data.rgba.pixel_step = 4;
  rb->data.rgba.row_step = rb->data.rgba.pixel_step * width;
  rb->width = width;
  rb->height = height;
  int nBytes = rb->data.rgba.row_step * height;
  rb->data.rgba.pixels = (band_type*)msSmallCalloc(nBytes,sizeof(band_type));
  rb->data.rgba.r = &(rb->data.rgba.pixels[band_order::R]);
  rb->data.rgba.g = &(rb->data.rgba.pixels[band_order::G]);
  rb->data.rgba.b = &(rb->data.rgba.pixels[band_order::B]);

  return MS_SUCCESS;
}

int utfgridGetRasterBufferHandle(imageObj *img, rasterBufferObj * rb)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);
  rb->type =MS_BUFFER_BYTE_RGBA;
  rb->data.rgba.pixels = r->buffer;
  rb->data.rgba.row_step = r->m_rendering_buffer.stride();
  rb->data.rgba.pixel_step = 4;
  rb->width = r->m_rendering_buffer.width();
  rb->height = r->m_rendering_buffer.height();
  rb->data.rgba.r = &(r->buffer[band_order::R]);
  rb->data.rgba.g = &(r->buffer[band_order::G]);
  rb->data.rgba.b = &(r->buffer[band_order::B]);
  rb->data.rgba.a = NULL;

  return MS_SUCCESS;
}

/*
 * Incase of error, free the memory used by image
 * 
 */
int freeImageUTFGrid(imageObj *img)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);
  free(r->buffer);
  delete r;
  img->img.plugin = NULL;
  return MS_SUCCESS;
}

int getTruetypeTextBBoxUTFGrid(rendererVTableObj *renderer, char **fonts, int numfonts, double size, char *string, rectObj *rect, double **advances,int bAdjustBaseline)
{
  return MS_SUCCESS;
}

int startNewLayerUTFGrid(imageObj *img, mapObj *map, layerObj *layer)
{
	return MS_SUCCESS;
}

int closeNewLayerUTFGrid(imageObj *img, mapObj *map, layerObj *layer)
{
	return MS_SUCCESS;
}

int utfgridRenderGlyphs(imageObj *img, double x, double y, labelStyleObj *style, char *text)
{
  return MS_SUCCESS;
}

/*
 * Fills the driver Vtable
 * 
 */
int msPopulateRendererVTableUTFGrid( rendererVTableObj *renderer )
{
  renderer->supports_transparent_layers = 0;
  renderer->supports_pixel_buffer = 1;
  renderer->use_imagecache = 0;
  renderer->supports_clipping = 0;
  renderer->supports_svg = 0;
  renderer->renderer_data = NULL;
  renderer->default_transform_mode = MS_TRANSFORM_SIMPLIFY;

  renderer->createImage=&createImageUTFGrid;
  renderer->saveImage=&saveImageUTFGrid;
  renderer->freeImage=&freeImageUTFGrid;

  renderer->getRasterBufferHandle = &utfgridGetRasterBufferHandle;
  renderer->initializeRasterBuffer = utfgridInitializeRasterBuffer;

  renderer->renderPolygon=&renderPolygonUTFGrid;
  renderer->renderLine=&renderLineUTFGrid;

  renderer->getTruetypeTextBBox=&getTruetypeTextBBoxUTFGrid;

  renderer->startLayer=&startNewLayerUTFGrid;
  renderer->endLayer=&closeNewLayerUTFGrid;

  return MS_SUCCESS;
}