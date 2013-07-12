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

typedef struct shapeData {
  char *datavalues;
  char *itemvalue;
  colorObj color;
  int serialid;
}
shapeData;

typedef struct lookupTable {
  shapeData  *table;
  int size;
  int counter;
}
lookupTable;

typedef struct UTFGridRenderer {   
  lookupTable *data;
  int utfresolution;
  int imagewidth;
  int imageheight;
  int layerwatch;
  int renderlayer;
  int useutfitem;
  outputFormatObj *aggFakeOutput;
  imageObj *aggImage;
  layerObj *utflayer;
}
UTFGridRenderer;

#define UTFGRID_RENDERER(image) ((UTFGridRenderer*) (image)->img.plugin)

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
  msFree(data->table);
  msFree(data);
  return MS_SUCCESS;
}

int addtotable(UTFGridRenderer *r, shapeObj *p, colorObj *color)
{
  for(int i=0; i<r->data->counter; i++) {
    if(!strcmp(p->values[r->utflayer->utfitemindex],r->data->table[i].itemvalue)) {
      color->red = r->data->table[i].color.red;
      color->green = r->data->table[i].color.green;
      color->blue = r->data->table[i].color.blue;

      return MS_SUCCESS;
    }
  }
  color->red = (r->data->counter+1) & 0x000000ff;
  color->green = (r->data->counter+1) & 0x0000ff00 / 0x100;
  color->blue = (r->data->counter+1) & 0x00ff0000 / 0x10000;

  r->data->table[r->data->counter].datavalues = msEvalTextExpression(&r->utflayer->utfdata, p);

  if(r->useutfitem)
    r->data->table[r->data->counter].itemvalue =  msStrdup(p->values[r->utflayer->utfitemindex]);

  r->data->table[r->data->counter].serialid = r->data->counter+1;

  r->data->counter++;

  return MS_SUCCESS;
}

/*
 * Create an imageObj, create a fake outputFormatObj to use with AGG, initialize driver.
 *
 */
imageObj *createImageUTFGrid(int width, int height, outputFormatObj *format, colorObj* bg)
{
  UTFGridRenderer *r;
  r = (UTFGridRenderer *) msSmallMalloc(sizeof(UTFGridRenderer));

  r->data = initTable();

  r->utfresolution = atof(msGetOutputFormatOption(format, "UTFRESOLUTION", "4"));
  r->imagewidth = width;
  r->imageheight = height;

  r->layerwatch = 0;

  r->useutfitem = 0;

  r->aggFakeOutput = (outputFormatObj *) msSmallMalloc(sizeof(outputFormatObj));
  r->aggFakeOutput->bands = 1;
  r->aggFakeOutput->name = msStrdup("aggFakeOutput");
  r->aggFakeOutput->driver = msStrdup("AGG/PNG");
  r->aggFakeOutput->refcount = 0;
  r->aggFakeOutput->vtable = NULL;
  r->aggFakeOutput->device = NULL;
  r->aggFakeOutput->transparent = 1;
  r->aggFakeOutput->imagemode = MS_IMAGEMODE_RGB;
  r->aggFakeOutput->mimetype = msStrdup("image/png");
  r->aggFakeOutput->extension = msStrdup("png");
  r->aggFakeOutput->renderer = MS_RENDER_WITH_AGG;
  if(MS_RENDERER_PLUGIN(r->aggFakeOutput)) {
    msInitializeRendererVTable(r->aggFakeOutput);
  }
  r->aggFakeOutput->formatoptions = (char **)msSmallMalloc(sizeof(char *));
  r->aggFakeOutput->numformatoptions = 0;
  msSetOutputFormatOption(r->aggFakeOutput, "GAMMA", "0.00001");
  msSetOutputFormatOption(r->aggFakeOutput, "ALIAS", "1");

  // r->aggImage = r->aggFakeOutput->vtable->createImage(width/r->utfresolution, height/r->utfresolution, r->aggFakeOutput, bg);
  r->aggImage = r->aggFakeOutput->vtable->createImage(width, height, r->aggFakeOutput, bg);

  r->utflayer = NULL;

  imageObj *image = NULL;
  image = (imageObj *) msSmallMalloc(sizeof(imageObj));
  image->img.plugin = (void*) r;

  return image;
}

/*
 * Draw the UTFGrid from AGG raster buffer datas.
 *
 */
int saveImageUTFGrid(imageObj *img, mapObj *map, FILE *fp, outputFormatObj *format)
{
  rasterBufferObj *rb;
  int row, col, i, waterPresence;
  unsigned short value;
  unsigned char *r,*g,*b,pixelid;
  int *rowdata, *prowdata;
  
  UTFGridRenderer *renderer = UTFGRID_RENDERER(img);

  rb = (rasterBufferObj *)msSmallMalloc(sizeof(rasterBufferObj *));
  renderer->aggFakeOutput->vtable->getRasterBufferHandle(renderer->aggImage, rb);

  rowdata = (int *)msSmallMalloc(sizeof(int *)*rb->width+1);

  fprintf(stdout, "{\"grid\":[");

  waterPresence = 0;  
  for(row=0; row<rb->height; row++) {
    r=rb->data.rgba.r+row*rb->data.rgba.row_step;
    g=rb->data.rgba.g+row*rb->data.rgba.row_step;
    b=rb->data.rgba.b+row*rb->data.rgba.row_step;

    if(row!=0)
      fprintf(stdout, ",");
    
    prowdata = rowdata;
    for(col=0; col<rb->width; col++) {
        pixelid = (*r + (*g)*0x100 + (*b)*0x10000) + 32;

      if(pixelid == 32) {
        waterPresence = 1;
      } 
      if(pixelid >= 34) {
        pixelid = pixelid +1;
      }
      if (pixelid >= 92) {
        pixelid = pixelid +1;
      }

      r+=rb->data.rgba.pixel_step;
      g+=rb->data.rgba.pixel_step;
      b+=rb->data.rgba.pixel_step;

      *prowdata = pixelid;
      prowdata++;
    }
    msConvertWideStringToUTF8 (rowdata, "UTF-8");

    fprintf(stdout, "\"%ls\"", rowdata);
  }

  fprintf(stdout, "],\"keys\":[");

  if(waterPresence==1) 
    fprintf(stdout, "\"\",");

  for(i=0;i<renderer->data->counter;i++) {  
    if(i!=0)
      fprintf(stdout, ",");

    if(renderer->useutfitem)
      fprintf(stdout, "\"%s\"", renderer->data->table[i].itemvalue);
    else
      fprintf(stdout, "\"%i\"", renderer->data->table[i].serialid);
  }

  fprintf(stdout, "],\"data\":{");

  for(i=0;i<renderer->data->counter;i++) {
    if(i!=0)
      fprintf(stdout, ",");

    if(renderer->useutfitem)
      fprintf(stdout, "\"%s\":", renderer->data->table[i].itemvalue);
    else
      fprintf(stdout, "\"%i\":", renderer->data->table[i].serialid);
    fprintf(stdout, "%s", renderer->data->table[i].datavalues);
  }

  fprintf(stdout, "}}");

  return MS_SUCCESS;
}

/*
 * Incase of error, free the memory used by UTFGrid driver
 * 
 */
int freeImageUTFGrid(imageObj *img)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);

  r->aggFakeOutput->vtable->freeImage(r->aggImage);
  r->aggFakeOutput->vtable->cleanup(MS_RENDERER_CACHE(r->aggFakeOutput->vtable));
  msFree(r->aggFakeOutput->vtable);
  msFree(r->aggFakeOutput->name);
  msFree(r->aggFakeOutput->mimetype);
  msFree(r->aggFakeOutput->driver);
  msFree(r->aggFakeOutput->extension);
  msFreeCharArray(r->aggFakeOutput->formatoptions, r->aggFakeOutput->numformatoptions);
  msFree(r->aggFakeOutput);

  msFree(r);

  freeTable(r->data);

  return MS_SUCCESS;
}

/*
 * Render polygon shapes with UTFGrid. Uses color bytes to carry table ID.
 *
 */
int renderPolygonUTFGrid(imageObj *img, shapeObj *p, colorObj *color)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);  

  growTable(r->data);

  addtotable(r, p, color);

  r->aggFakeOutput->vtable->renderPolygon(r->aggImage, p, color);

  return MS_SUCCESS;
}

/*
 * Return success to avoid error message.
 *
 */
int renderLineUTFGrid(imageObj *img, shapeObj *p, strokeStyleObj *stroke)
{
  if(p->type == MS_SHAPE_POLYGON)
    return MS_SUCCESS;

  UTFGridRenderer *r = UTFGRID_RENDERER(img);

  growTable(r->data);

  addtotable(r, p, stroke->color);

  return MS_SUCCESS;
}

/*
 * Initialize raster buffer for AGG uses. Using generic type which are equivalent instead of AGG
 * specific types.
 *
 */
int utfgridInitializeRasterBuffer(rasterBufferObj *rb, int width, int height, int mode)
{
  rb->type = MS_BUFFER_BYTE_RGBA;
  rb->data.rgba.pixel_step = 4;
  rb->data.rgba.row_step = rb->data.rgba.pixel_step * width;
  rb->width = width;
  rb->height = height;
  int nBytes = rb->data.rgba.row_step * height;
  rb->data.rgba.pixels = (unsigned char*)msSmallCalloc(nBytes,sizeof(unsigned char*));
  rb->data.rgba.r = &(rb->data.rgba.pixels[2]);
  rb->data.rgba.g = &(rb->data.rgba.pixels[1]);
  rb->data.rgba.b = &(rb->data.rgba.pixels[0]);

  return MS_SUCCESS;
}

/*
 * Return success to avoid error message.
 *
 */
int getTruetypeTextBBoxUTFGrid(rendererVTableObj *renderer, char **fonts, int numfonts, double size, char *string, rectObj *rect, double **advances,int bAdjustBaseline)
{
  return MS_SUCCESS;
}

/*
 * Return success to avoid error message.
 *
 */
int startNewLayerUTFGrid(imageObj *img, mapObj *map, layerObj *layer)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);

  if(!r->layerwatch) {
    r->utflayer = layer;
    layer->refcount++;
    if(r->utflayer->utfitem)
      r->useutfitem = 1;
    
    r->layerwatch = 1;
  }

	return MS_SUCCESS;
}

/*
 * Return success to avoid error message.
 *
 */
int closeNewLayerUTFGrid(imageObj *img, mapObj *map, layerObj *layer)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);

  if(r->layerwatch) {
    r->utflayer = NULL;
    layer->refcount--;
  }

	return MS_SUCCESS;
}

/*
 * Return success to avoid error message.
 *
 */
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
  renderer->default_transform_mode = MS_TRANSFORM_SIMPLIFY;

  renderer->createImage=&createImageUTFGrid;
  renderer->saveImage=&saveImageUTFGrid;
  renderer->freeImage=&freeImageUTFGrid;

  renderer->renderPolygon=&renderPolygonUTFGrid;
  renderer->renderLine=&renderLineUTFGrid;

  renderer->initializeRasterBuffer = utfgridInitializeRasterBuffer;

  renderer->getTruetypeTextBBox=&getTruetypeTextBBoxUTFGrid;

  renderer->startLayer=&startNewLayerUTFGrid;
  renderer->endLayer=&closeNewLayerUTFGrid;

  renderer->renderGlyphs = &utfgridRenderGlyphs;

  return MS_SUCCESS;
}