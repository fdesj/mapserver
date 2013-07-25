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


#include "mapserver.h"
// #include "mapagg.h"
#include "maputfgrid.h"
#include "renderers/agg/include/agg_rasterizer_scanline_aa.h"
#include "renderers/agg/include/agg_basics.h"
#include "renderers/agg/include/agg_renderer_scanline.h"
#include "renderers/agg/include/agg_scanline_bin.h"
#include "renderers/agg/include/agg_conv_stroke.h"
#include "renderers/agg/include/agg_path_storage.h"
#include "renderers/agg/include/agg_ellipse.h"


typedef mapserver::int32u band_type;
typedef mapserver::row_ptr_cache<band_type> rendering_buffer;
typedef blender_utf<utfpix32> blender_utf32;
typedef pixfmt_alpha_blend_utf<blender_utf32, rendering_buffer> pixfmt_utf32;
typedef mapserver::rasterizer_scanline_aa<> rasterizer_scanline;
typedef mapserver::renderer_base<pixfmt_utf32> renderer_base;
typedef mapserver::renderer_scanline_bin_solid<renderer_base> renderer_scanline;

static utfpix32 UTF_WATER = utfpix32(32, 0);

#define utfitem(c) utfpix32(c, 0)


struct shapeData 
{
  char *datavalues;
  char *itemvalue;
  band_type utfvalue;
  int serialid;
};

struct lookupTable {
  shapeData  *table;
  int size;
  int counter;
};

class UTFGridRenderer {
public:
  UTFGridRenderer()
  {
    stroke = NULL;
  }
  ~UTFGridRenderer()
  {
    if(stroke)
      delete stroke;
  }

  lookupTable *data;
  int utfresolution;
  int layerwatch;
  int renderlayer;
  int useutfitem;
  int duplicates;
  layerObj *utflayer;
  band_type *buffer;
  rendering_buffer m_rendering_buffer;
  pixfmt_utf32 m_pixel_format;
  renderer_base m_renderer_base;
  rasterizer_scanline m_rasterizer;
  renderer_scanline m_renderer_scanline;
  mapserver::scanline_bin sl_utf;
  mapserver::conv_stroke<line_adaptor_utf> *stroke;
};

#define UTFGRID_RENDERER(image) ((UTFGridRenderer*) (image)->img.plugin)

int encodeForRendering(unsigned int &encode)
{
  encode += 32;
  if(encode >= 34) {
    encode = encode +1;
  }
  if (encode >= 92) {
    encode = encode +1;
  }
  return MS_SUCCESS;
}

lookupTable *initTable()
{
  lookupTable *data;
  data = (lookupTable *) msSmallMalloc(sizeof(lookupTable));
  data->table = (shapeData *) msSmallCalloc(1,sizeof(shapeData));
  data->counter = 0;
  data->size = 1;
  return data;
}

int growTable(lookupTable *data)
{
  if(data->size == data->counter) {
    data->table = (shapeData*) msSmallRealloc(data->table,sizeof(*data->table)*data->size*2);
    data->size = data->size*2;
  }
  return MS_SUCCESS;
}

int freeTable(lookupTable *data)
{
  int i;
  for(i=0;i<data->counter;i++) {
    msFree(data->table[i].datavalues);
    msFree(data->table[i].itemvalue);
  }
  msFree(data->table);
  msFree(data);
  return MS_SUCCESS;
}

int addToTable(UTFGridRenderer *r, shapeObj *p, band_type &value)
{
  if(r->duplicates==0) {
    int i;
    for(i=0; i<r->data->counter; i++) {
      if(!strcmp(p->values[r->utflayer->utfitemindex],r->data->table[i].itemvalue)) {
        value = r->data->table[i].utfvalue;

        return MS_SUCCESS;
      }
    }
  }
  value = (r->data->counter+1);

  r->data->table[r->data->counter].datavalues = msEvalTextExpression(&r->utflayer->utfdata, p);

  if(r->useutfitem)
    r->data->table[r->data->counter].itemvalue =  msStrdup(p->values[r->utflayer->utfitemindex]);

  r->data->table[r->data->counter].serialid = r->data->counter+1;

  r->data->table[r->data->counter].utfvalue = value;

  r->data->counter++;

  return MS_SUCCESS;
}

imageObj *utfgridCreateImage(int width, int height, outputFormatObj *format, colorObj * bg)
{
  UTFGridRenderer *r;
  r = new UTFGridRenderer;

  r->data = initTable();

  r->utfresolution = atof(msGetOutputFormatOption(format, "UTFRESOLUTION", "4"));

  r->layerwatch = 0;

  r->renderlayer = 0;

  r->useutfitem = 0;

  r->duplicates = strcmp("false", msGetOutputFormatOption(format, "DUPLICATES", "true"));

  r->buffer = (band_type*)msSmallMalloc(width/r->utfresolution * height/r->utfresolution * sizeof(band_type));

  r->m_rendering_buffer.attach(r->buffer, width/r->utfresolution, height/r->utfresolution, width/r->utfresolution);
  r->m_pixel_format.attach(r->m_rendering_buffer);
  r->m_renderer_base.attach(r->m_pixel_format);
  r->m_renderer_scanline.attach(r->m_renderer_base);
  r->m_renderer_base.clear(UTF_WATER);
  r->m_rasterizer.gamma(mapserver::gamma_none());

  r->utflayer = NULL;

  imageObj *image = NULL;
  image = (imageObj *) msSmallCalloc(1,sizeof(imageObj));
  image->img.plugin = (void*) r;

  return image;
}

int utfgridFreeImage(imageObj *img)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);

  freeTable(r->data);  
  msFree(r->buffer);
  delete r;

  img->img.plugin = NULL;

  return MS_SUCCESS;
}

int utfgridSaveImage(imageObj *img, mapObj *map, FILE *fp, outputFormatObj *format)
{
  int row, col, i, waterPresence;
  band_type pixelid;
 
  UTFGridRenderer *renderer = UTFGRID_RENDERER(img);

  printf("{\"grid\":[");

  waterPresence = 0;  
  for(row=0; row<img->height/renderer->utfresolution; row++) {

    if(row!=0)
      printf(",");
    printf("\"");
    for(col=0; col<img->width/renderer->utfresolution; col++) {
      pixelid = renderer->buffer[(row*img->width/renderer->utfresolution)+col];

      if(pixelid == 32) {
        waterPresence = 1;
      } 

      wchar_t s[2]= {pixelid};
      s[1] = '\0';  
      char * utf8;
      utf8 = msConvertWideStringToUTF8 (s, "UCS-4-INTERNAL");
      printf("%s", utf8);
      msFree(utf8);
    }

    printf("\"");
  }

  printf("],\"keys\":[");

  if(waterPresence==1) 
    printf("\"\",");

  for(i=0;i<renderer->data->counter;i++) {  
    if(i!=0)
      printf(",");

    if(renderer->useutfitem)
      printf("\"%s\"", renderer->data->table[i].itemvalue);
    else
      printf("\"%i\"", renderer->data->table[i].serialid);
  }

  fprintf(stdout, "],\"data\":{");

  for(i=0;i<renderer->data->counter;i++) {
    if(i!=0)
      printf(",");

    if(renderer->useutfitem)
      printf("\"%s\":", renderer->data->table[i].itemvalue);
    else
      printf("\"%i\":", renderer->data->table[i].serialid);
    printf("%s", renderer->data->table[i].datavalues);
  }

  printf("}}");

  return MS_SUCCESS;
}

int utfgridStartLayer(imageObj *img, mapObj *map, layerObj *layer)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);

  if(&layer->utfdata!=NULL) {

    if(!r->layerwatch) {
      r->renderlayer = 1;
      r->utflayer = layer;
      layer->refcount++;

      if(r->utflayer->utfitem)
        r->useutfitem = 1;
    
      r->layerwatch = 1;
    }
    else {
      msSetError(MS_MISCERR, "MapServer does not support multiple UTFGrid layers", "utfgridStartLayer()");
      return MS_FAILURE;
    }
  }

  return MS_SUCCESS;
}

int utfgridEndLayer(imageObj *img, mapObj *map, layerObj *layer)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);

  if(r->renderlayer) {
    r->utflayer = NULL;
    layer->refcount--;
    r->renderlayer = 0;
  }

  return MS_SUCCESS;
}

int utfgridGetTruetypeTextBBox(rendererVTableObj *renderer, char **fonts, int numfonts, double size, char *string, rectObj *rect, double **advances,int bAdjustBaseline)
{
  return MS_SUCCESS;
}

int utfgridRenderGlyphs(imageObj *img, double x, double y, labelStyleObj *style, char *text)
{
  return MS_SUCCESS;
}

int utfgridRenderPolygon(imageObj *img, shapeObj *p, colorObj *color)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);  
  band_type value, test;

  growTable(r->data);

  addToTable(r, p, value);

  encodeForRendering(value);

  polygon_adaptor_utf polygons(p, r->utfresolution);

  r->m_rasterizer.reset();
  r->m_rasterizer.filling_rule(mapserver::fill_even_odd);
  r->m_rasterizer.add_path(polygons);
  r->m_renderer_scanline.color(utfitem(value));
  mapserver::render_scanlines(r->m_rasterizer, r->sl_utf, r->m_renderer_scanline);

  return MS_SUCCESS;
}

int utfgridRenderLine(imageObj *img, shapeObj *p, strokeStyleObj *stroke)
{
  if(p->type == MS_SHAPE_POLYGON)
    return MS_SUCCESS;

  UTFGridRenderer *r = UTFGRID_RENDERER(img);
  band_type value;

  growTable(r->data);

  addToTable(r, p, value);

  encodeForRendering(value);

  line_adaptor_utf lines(p, r->utfresolution);

  r->m_rasterizer.reset();
  r->m_rasterizer.filling_rule(mapserver::fill_non_zero);  
  if(!r->stroke) {
    r->stroke = new mapserver::conv_stroke<line_adaptor_utf>(lines);
  } else {
    r->stroke->attach(lines);
  }
  r->stroke->width(stroke->width);
  r->m_rasterizer.add_path(*r->stroke);
  r->m_renderer_scanline.color(utfitem(value));
  mapserver::render_scanlines(r->m_rasterizer, r->sl_utf, r->m_renderer_scanline);

  return MS_SUCCESS;
}

// static mapserver::path_storage imageVectorSymbolAGG(symbolObj *symbol)
// {
//   mapserver::path_storage path;
//   bool is_new=true;
//   for(int i=0; i < symbol->numpoints; i++) {
//     if((symbol->points[i].x == -99) && (symbol->points[i].y == -99)) { // (PENUP)
//       is_new=true;
//     } else {
//       if(is_new) {
//         path.move_to(symbol->points[i].x,symbol->points[i].y);
//         is_new=false;
//       } else {
//         path.line_to(symbol->points[i].x,symbol->points[i].y);
//       }
//     }
//   }
//   return path;
// }

int utfgridRenderVectorSymbol(imageObj *img, double x, double y, symbolObj *symbol, symbolStyleObj * style)
{
  return MS_SUCCESS;
}

// int agg2RenderVectorSymbol(imageObj *img, double x, double y, symbolObj *symbol, symbolStyleObj * style)
// {
//   AGG2Renderer *r = AGG_RENDERER(img);
//   double ox = symbol->sizex * 0.5;
//   double oy = symbol->sizey * 0.5;

//   mapserver::path_storage path = imageVectorSymbolAGG(symbol);
//   mapserver::trans_affine mtx;
//   mtx *= mapserver::trans_affine_translation(-ox,-oy);
//   mtx *= mapserver::trans_affine_scaling(style->scale);
//   mtx *= mapserver::trans_affine_rotation(-style->rotation);
//   mtx *= mapserver::trans_affine_translation(x, y);
//   path.transform(mtx);
//     r->m_rasterizer_aa.reset();
//     r->m_rasterizer_aa.filling_rule(mapserver::fill_even_odd);
//     r->m_rasterizer_aa.add_path(path);
//     r->m_renderer_scanline.color(aggColor(style->color));
//     mapserver::render_scanlines(r->m_rasterizer_aa, r->sl_poly, r->m_renderer_scanline);
//   return MS_SUCCESS;
// }

int utfgridRenderPixmapSymbol(imageObj *img, double x, double y, symbolObj *symbol, symbolStyleObj * style)
{
  return MS_SUCCESS;
}

// int agg2RenderPixmapSymbol(imageObj *img, double x, double y, symbolObj *symbol, symbolStyleObj * style)
// {
//   AGG2Renderer *r = AGG_RENDERER(img);
//   rasterBufferObj *pixmap = symbol->pixmap_buffer;
//   assert(pixmap->type == MS_BUFFER_BYTE_RGBA);
//   rendering_buffer b(pixmap->data.rgba.pixels,pixmap->width,pixmap->height,pixmap->data.rgba.row_step);
//   pixel_format pf(b);

//   r->m_rasterizer_aa.reset();
//   r->m_rasterizer_aa.filling_rule(mapserver::fill_non_zero);
//   if ( (style->rotation != 0 && style->rotation != MS_PI*2.)|| style->scale != 1) {
//     mapserver::trans_affine image_mtx;
//     image_mtx *= mapserver::trans_affine_translation(-(pf.width()/2.),-(pf.height()/2.));
//     /*agg angles are antitrigonometric*/
//     image_mtx *= mapserver::trans_affine_rotation(-style->rotation);
//     image_mtx *= mapserver::trans_affine_scaling(style->scale);



//     image_mtx *= mapserver::trans_affine_translation(x,y);
//     image_mtx.invert();
//     typedef mapserver::span_interpolator_linear<> interpolator_type;
//     interpolator_type interpolator(image_mtx);
//     mapserver::span_allocator<color_type> sa;

//     // "hardcoded" bilinear filter
//     //------------------------------------------
//     typedef mapserver::span_image_filter_rgba_bilinear_clip<pixel_format, interpolator_type> span_gen_type;
//     span_gen_type sg(pf, mapserver::rgba(0,0,0,0), interpolator);
//     mapserver::path_storage pixmap_bbox;
//     int ims_2 = MS_NINT(MS_MAX(pixmap->width,pixmap->height)*style->scale*1.415)/2+1;

//     pixmap_bbox.move_to(x-ims_2,y-ims_2);
//     pixmap_bbox.line_to(x+ims_2,y-ims_2);
//     pixmap_bbox.line_to(x+ims_2,y+ims_2);
//     pixmap_bbox.line_to(x-ims_2,y+ims_2);

//     r->m_rasterizer_aa.add_path(pixmap_bbox);
//     mapserver::render_scanlines_aa(r->m_rasterizer_aa, r->sl_poly, r->m_renderer_base, sa, sg);
//   } else {
//     //just copy the image at the correct location (we place the pixmap on
//     //the nearest integer pixel to avoid blurring)
//     r->m_renderer_base.blend_from(pf,0,MS_NINT(x-pixmap->width/2.),MS_NINT(y-pixmap->height/2.));
//   }
//   return MS_SUCCESS;
// }

int utfgridRenderEllipseSymbol(imageObj *image, double x, double y, symbolObj *symbol, symbolStyleObj * style)
{
  // UTFGridRenderer *r = UTFGRID_RENDERER(image);
  // band_type value;

  // growTable(r->data);

  // addToTable(r, p, value);

  // encodeForRendering(value);

  // mapserver::path_storage path;
  // mapserver::ellipse ellipse(x,y,symbol->sizex*style->scale/2,symbol->sizey*style->scale/2);
  // path.concat_path(ellipse);
  // if( style->rotation != 0) {
  //   mapserver::trans_affine mtx;
  //   mtx *= mapserver::trans_affine_translation(-x,-y);
  //   mtx *= mapserver::trans_affine_rotation(-style->rotation);
  //   mtx *= mapserver::trans_affine_translation(x,y);
  //   path.transform(mtx);
  // }
  return MS_SUCCESS;
}

// int agg2RenderEllipseSymbol(imageObj *image, double x, double y, symbolObj *symbol, symbolStyleObj * style)
// {
//   if( style->rotation != 0) {
//     mapserver::trans_affine mtx;
//     mtx *= mapserver::trans_affine_translation(-x,-y);
//     mtx *= mapserver::trans_affine_rotation(-style->rotation);
//     mtx *= mapserver::trans_affine_translation(x,y);
//     path.transform(mtx);
//   }
//     r->m_rasterizer_aa.reset();
//     r->m_rasterizer_aa.filling_rule(mapserver::fill_even_odd);
//     r->m_rasterizer_aa.add_path(path);
//     r->m_renderer_scanline.color(aggColor(style->color));
//     mapserver::render_scanlines(r->m_rasterizer_aa, r->sl_line, r->m_renderer_scanline);
//   return MS_SUCCESS;
// }

int utfgridRenderTruetypeSymbol(imageObj *img, double x, double y, symbolObj *symbol, symbolStyleObj * style) 
{
  return MS_SUCCESS;
}

// int agg2RenderTruetypeSymbol(imageObj *img, double x, double y, symbolObj *symbol, symbolStyleObj * style)
// {
//   AGG2Renderer *r = AGG_RENDERER(img);
//   aggRendererCache *cache = (aggRendererCache*)MS_RENDERER_CACHE(MS_IMAGE_RENDERER(img));
//   if(aggLoadFont(cache,symbol->full_font_path,style->scale) == MS_FAILURE)
//     return MS_FAILURE;

//   int unicode;
//   font_curve_type m_curves(cache->m_fman.path_adaptor());

//   msUTF8ToUniChar(symbol->character, &unicode);
//   const mapserver::glyph_cache* glyph = cache->m_fman.glyph(unicode);
//   double ox = (glyph->bounds.x1 + glyph->bounds.x2) / 2.;
//   double oy = (glyph->bounds.y1 + glyph->bounds.y2) / 2.;

//   mapserver::trans_affine mtx = mapserver::trans_affine_translation(-ox, -oy);
//   if(style->rotation)
//     mtx *= mapserver::trans_affine_rotation(-style->rotation);
//   mtx *= mapserver::trans_affine_translation(x, y);

//   mapserver::path_storage glyphs;

//   cache->m_fman.init_embedded_adaptors(glyph, 0,0);
//   mapserver::conv_transform<font_curve_type, mapserver::trans_affine> trans_c(m_curves, mtx);
//   glyphs.concat_path(trans_c);
//   if (style->outlinecolor) {
//     r->m_rasterizer_aa.reset();
//     r->m_rasterizer_aa.filling_rule(mapserver::fill_non_zero);
//     mapserver::conv_contour<mapserver::path_storage> cc(glyphs);
//     cc.auto_detect_orientation(true);
//     cc.width(style->outlinewidth + 1);
//     r->m_rasterizer_aa.add_path(cc);
//     r->m_renderer_scanline.color(aggColor(style->outlinecolor));
//     mapserver::render_scanlines(r->m_rasterizer_aa, r->sl_line, r->m_renderer_scanline);
//   }

//   if (style->color) {
//     r->m_rasterizer_aa.reset();
//     r->m_rasterizer_aa.filling_rule(mapserver::fill_non_zero);
//     r->m_rasterizer_aa.add_path(glyphs);
//     r->m_renderer_scanline.color(aggColor(style->color));
//     mapserver::render_scanlines(r->m_rasterizer_aa, r->sl_line, r->m_renderer_scanline);
//   }
//   return MS_SUCCESS;

// }

int msPopulateRendererVTableUTFGrid( rendererVTableObj *renderer )
{
  renderer->default_transform_mode = MS_TRANSFORM_SIMPLIFY;

  renderer->createImage = &utfgridCreateImage;
  renderer->freeImage = &utfgridFreeImage;
  renderer->saveImage = &utfgridSaveImage;

  renderer->startLayer = &utfgridStartLayer;
  renderer->endLayer = &utfgridEndLayer;

  renderer->getTruetypeTextBBox = &utfgridGetTruetypeTextBBox;

  renderer->renderGlyphs = &utfgridRenderGlyphs;
  renderer->renderPolygon = &utfgridRenderPolygon;
  renderer->renderLine = &utfgridRenderLine;
  renderer->renderVectorSymbol = &utfgridRenderVectorSymbol;
  renderer->renderPixmapSymbol = &utfgridRenderPixmapSymbol;
  renderer->renderEllipseSymbol = &utfgridRenderEllipseSymbol;
  renderer->renderTruetypeSymbol = &utfgridRenderTruetypeSymbol;

  return MS_SUCCESS;
}