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
#include "mapagg.h"
#include "maputfgrid.h"
#include "renderers/agg/include/agg_rendering_buffer.h"
#include "renderers/agg/include/agg_rasterizer_scanline_aa.h"
#include "renderers/agg/include/agg_basics.h"
#include "renderers/agg/include/agg_renderer_scanline.h"
#include "renderers/agg/include/agg_scanline_bin.h"

typedef mapserver::int32u band_type;
typedef mapserver::row_ptr_cache<band_type> rendering_buffer;
typedef blender_utf<utfpix32> blender_utf32;
typedef pixfmt_alpha_blend_utf<blender_utf32, rendering_buffer> pixfmt_utf32;
typedef mapserver::rasterizer_scanline_aa<> rasterizer_scanline;
typedef mapserver::renderer_base<pixfmt_utf32> renderer_base;
typedef mapserver::renderer_scanline_bin_solid<renderer_base> renderer_scanline;

static utfpix32 UTF_WATER = utfpix32(0, 0);

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

struct UTFGridRenderer {   
  lookupTable *data;
  int utfresolution;
  int imagewidth;
  int imageheight;
  int layerwatch;
  int renderlayer;
  int useutfitem;
  layerObj *utflayer;
  band_type *buffer;
  rendering_buffer m_rendering_buffer;
  pixfmt_utf32 m_pixel_format;
  renderer_base m_renderer_base;
  rasterizer_scanline m_rasterizer_scanline;
  renderer_scanline m_renderer_scanline;
  mapserver::scanline_bin sl_utf;
};

#define UTFGRID_RENDERER(image) ((UTFGridRenderer*) (image)->img.plugin)

unsigned int encodeToUTF8(unsigned int encode)
{
  if(encode < 0x80)
    return encode;
  else if(encode < 0x800) {
    unsigned int lowBits, highBits;
    lowBits = encode % 0x40 + 0x80;
    highBits = encode - encode % 0x40;
    highBits = (highBits << 2) + 0xC000;
    return lowBits+highBits;
  }
  return -1;
}

char * outputUTF8char(unsigned int split)
{
  char * output;
  if(split < 0x80) {
    output = (char *)msSmallCalloc(1,sizeof(char));
    *output = split & 0xFF;
    return output;
  }
  else if(split < 0x800) {
    output = (char *)msSmallCalloc(2,sizeof(char));
    *output = split & 0xFF;
    output++;
    *output = split & 0xFF00;
    return output;
  }
  return NULL;
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
  int i;
  for(i=0; i<r->data->counter; i++) {
    if(!strcmp(p->values[r->utflayer->utfitemindex],r->data->table[i].itemvalue)) {
      value = r->data->table[i].utfvalue;

      return MS_SUCCESS;
    }
  }
  value = (r->data->counter+1);

  r->data->table[r->data->counter].datavalues = msEvalTextExpression(&r->utflayer->utfdata, p);

  if(r->useutfitem)
    r->data->table[r->data->counter].itemvalue =  msStrdup(p->values[r->utflayer->utfitemindex]);

  r->data->table[r->data->counter].serialid = r->data->counter+1;

  r->data->counter++;

  return MS_SUCCESS;
}

imageObj *utfgridCreateImage(int width, int height, outputFormatObj *format, colorObj * bg)
{
  UTFGridRenderer *r;
  r = (UTFGridRenderer *) msSmallMalloc(sizeof(UTFGridRenderer));

  r->data = initTable();

  r->utfresolution = atof(msGetOutputFormatOption(format, "UTFRESOLUTION", "4"));
  r->imagewidth = width;
  r->imageheight = height;

  r->layerwatch = 0;

  r->renderlayer = 0;

  r->useutfitem = 0;

  r->buffer = (band_type*)msSmallMalloc(width * height * sizeof(band_type));

  r->m_rendering_buffer.attach(r->buffer, width, height, width);
  r->m_pixel_format.attach(r->m_rendering_buffer);
  r->m_renderer_base.attach(r->m_pixel_format);
  r->m_renderer_scanline.attach(r->m_renderer_base);
  r->m_renderer_base.clear(UTF_WATER);
  r->m_rasterizer_scanline.gamma(mapserver::gamma_none());

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
  msFree(r);

  img->img.plugin = NULL;

  return MS_SUCCESS;
}

int utfgridSaveImage(imageObj *img, mapObj *map, FILE *fp, outputFormatObj *format)
{
  int row, col, i, waterPresence;
  wchar_t *rowdata, *prowdata;
  band_type pixelid;
  char *utf_string;
 
  UTFGridRenderer *renderer = UTFGRID_RENDERER(img);

  rowdata = (wchar_t *)msSmallCalloc(img->width+1,sizeof(wchar_t));

  printf("{\"grid\":[");

  waterPresence = 0;  
  for(row=0; row<img->height; row++) {

    if(row!=0)
      printf(",");
    printf("\"");
    prowdata = rowdata;
    for(col=0; col<img->width; col++) {
        pixelid = renderer->buffer[(row*img->width)+col]+32;

      if(pixelid == 32) {
        waterPresence = 1;
      } 
      if(pixelid >= 34) {
        pixelid = pixelid +1;
      }
      if (pixelid >= 92) {
        pixelid = pixelid +1;
      }
      *prowdata = pixelid;
      prowdata++;
    }
    utf_string = msConvertWideStringToUTF8 (rowdata, "wchar_t");
    printf("%s", utf_string);
    printf("\"");
  }

  msFree(rowdata);

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

  value = encodeToUTF8(value);

  polygon_adaptor polygons(p);

  r->m_rasterizer_scanline.reset();
  r->m_rasterizer_scanline.filling_rule(mapserver::fill_even_odd);
  r->m_rasterizer_scanline.add_path(polygons);
  r->m_renderer_scanline.color(utfitem(value));
  mapserver::render_scanlines(r->m_rasterizer_scanline, r->sl_utf, r->m_renderer_scanline);

  return MS_SUCCESS;
}

int utfgridRenderLine(imageObj *img, shapeObj *p, strokeStyleObj *stroke)
{
  // if(p->type == MS_SHAPE_POLYGON)
  //   return MS_SUCCESS;

  UTFGridRenderer *r = UTFGRID_RENDERER(img);
  band_type value;

  growTable(r->data);

  addToTable(r, p, value);

  line_adaptor lines = line_adaptor(p);

  return MS_SUCCESS;
}

// if (style->patternlength <= 0) {
//     if(!r->stroke) {
//       r->stroke = new mapserver::conv_stroke<line_adaptor>(lines);
//     } else {
//       r->stroke->attach(lines);
//     }
//     r->stroke->width(style->width);
//     if(style->width>1) {
//       applyCJC(*r->stroke, style->linecap, style->linejoin);
//     } else {
//       r->stroke->inner_join(mapserver::inner_bevel);
//       r->stroke->line_join(mapserver::bevel_join);
//     }
//     r->m_rasterizer_aa.add_path(*r->stroke);
//   }

// switch (joins) {
//     case MS_CJC_ROUND:
//       stroke.line_join(mapserver::round_join);
//       break;
//     case MS_CJC_MITER:
//       stroke.line_join(mapserver::miter_join);
//       break;
//     case MS_CJC_BEVEL:
//     case MS_CJC_NONE:
//       stroke.line_join(mapserver::bevel_join);
//       break;
//   }
//   switch (caps) {
//     case MS_CJC_BUTT:
//     case MS_CJC_NONE:
//       stroke.line_cap(mapserver::butt_cap);
//       break;
//     case MS_CJC_ROUND:
//       stroke.line_cap(mapserver::round_cap);
//       break;
//     case MS_CJC_SQUARE:
//       stroke.line_cap(mapserver::square_cap);
//       break;

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

  return MS_SUCCESS;
}