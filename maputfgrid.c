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

typedef struct lookupTable {
  long int table[256];
  int counter;
}
lookupTable;

typedef struct UTFGridRenderer { 
  outputFormatObj *aggFakeOutput;
  void *aggRendererTool;
  lookupTable lookupTableData;
}
UTFGridRenderer;

#define UTFGRID_RENDERER(image) ((UTFGridRenderer*) (image)->img.plugin)

/*
 * Create an imageObj, create a fake outputFormatObj to use with AGG, initialize driver.
 *
 */
imageObj *createImageUTFGrid(int width, int height, outputFormatObj *format, colorObj* bg)
{
  imageObj *image = NULL;
  UTFGridRenderer *r;
  r = (UTFGridRenderer *) calloc(1, sizeof (UTFGridRenderer));
  MS_CHECK_ALLOC(r, sizeof (UTFGridRenderer), NULL);
  r->lookupTableData.counter = 0;

  /*
   *      Allocate the fake format object.           
   */
  r->aggFakeOutput = (outputFormatObj *) calloc(1,sizeof(outputFormatObj));
  if( r->aggFakeOutput == NULL ) {
    msSetError( MS_MEMERR, NULL, "createImageUTFGrid()" );
    return NULL;
  }
  r->aggFakeOutput->bands = 1;
  r->aggFakeOutput->name = msStrdup("aggFakeOutput");
  r->aggFakeOutput->driver = msStrdup("AGG/PNG");
  r->aggFakeOutput->refcount = 0;
  r->aggFakeOutput->vtable = NULL;
  r->aggFakeOutput->device = NULL;
  r->aggFakeOutput->transparent = 1;
  r->aggFakeOutput->imagemode = MS_IMAGEMODE_RGB;
  r->aggFakeOutput->mimetype = msStrdup("image/png");
  r->aggFakeOutput->imagemode = MS_IMAGEMODE_RGB;
  r->aggFakeOutput->extension = msStrdup("png");
  r->aggFakeOutput->renderer = MS_RENDER_WITH_AGG;
  if(MS_RENDERER_PLUGIN(r->aggFakeOutput)) {
    msInitializeRendererVTable(r->aggFakeOutput);
  }
  msSetOutputFormatOption(r->aggFakeOutput, "GAMMA", "0.00001");

  image = r->aggFakeOutput->vtable->createImage(width, height, r->aggFakeOutput, bg);
  r->aggRendererTool = (void*) image->img.plugin;
  image->img.plugin = (void*) r;
  format->vtable->renderer_data = (void*) &r->lookupTableData;
  r->lookupTableData.counter=0;

  return image;
}

/*
 * Draw the UTFGrid from AGG raster buffer datas.
 *
 */
int saveImageUTFGrid(imageObj *img, mapObj *map, FILE *fp, outputFormatObj *format)
{
  return MS_FAILURE;
}

/*
 * Render polygon shapes with UTFGrid. Uses color bytes to carry table ID.
 *
 */
int renderPolygonUTFGrid(imageObj *img, shapeObj *p, colorObj *color)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);  
  img->img.plugin = (void*) r->aggRendererTool;
  color->red = r->lookupTableData.counter & 0x000000ff;
  color->green = ((r->lookupTableData.counter & 0x0000ff00) / 0x100);
  color->blue = ((r->lookupTableData.counter & 0x00ff0000) / 0x10000);
  r->lookupTableData.table[r->lookupTableData.counter]=p->index;
  r->lookupTableData.counter++;
  r->aggFakeOutput->vtable->renderPolygon(img, p, color);
  img->img.plugin = (void*) r;
  return MS_SUCCESS;
}

/*
 * Return success to avoid error message.
 *
 */
int renderLineUTFGrid(imageObj *img, shapeObj *p, strokeStyleObj *stroke)
{
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
 * Get AGG raster buffer handle for rendering uses.
 *
 */
int utfgridGetRasterBufferHandle(imageObj *img, rasterBufferObj * rb)
{  
  UTFGridRenderer *r = UTFGRID_RENDERER(img);
  img->img.plugin = (void*) r->aggRendererTool;
  r->aggFakeOutput->vtable->getRasterBufferHandle(img, rb);
  img->img.plugin = (void*) r;
  return MS_SUCCESS;
}

/*
 * Incase of error, free the memory used by UTFGrid driver
 * 
 */
int freeImageUTFGrid(imageObj *img)
{
  UTFGridRenderer *r = UTFGRID_RENDERER(img);
  img->img.plugin = (void*) r->aggRendererTool;
  r->aggFakeOutput->vtable->freeImage(img);
  r->aggFakeOutput->vtable->cleanup(MS_RENDERER_CACHE(r->aggFakeOutput->vtable));
  free( r->aggFakeOutput->vtable);
  msFree(r->aggFakeOutput->name);
  msFree(r->aggFakeOutput->mimetype);
  msFree(r->aggFakeOutput->driver);
  msFree(r->aggFakeOutput->extension);
  msFreeCharArray(r->aggFakeOutput->formatoptions, r->aggFakeOutput->numformatoptions);
  msFree(r->aggFakeOutput);
  free(r);
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
	return MS_SUCCESS;
}

/*
 * Return success to avoid error message.
 *
 */
int closeNewLayerUTFGrid(imageObj *img, mapObj *map, layerObj *layer)
{
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
  renderer->supports_transparent_layers = 0;
  renderer->supports_pixel_buffer = 1;
  renderer->use_imagecache = 0;
  renderer->supports_clipping = 0;
  renderer->supports_svg = 0;
  renderer->default_transform_mode = MS_TRANSFORM_SIMPLIFY;

  renderer->createImage=&createImageUTFGrid;
  renderer->saveImage=&saveImageUTFGrid;
  renderer->freeImage=&freeImageUTFGrid;

  renderer->getRasterBufferHandle = &utfgridGetRasterBufferHandle;
  renderer->initializeRasterBuffer = utfgridInitializeRasterBuffer;

  renderer->renderPolygon=&renderPolygonUTFGrid;
  renderer->renderLine=&renderLineUTFGrid;
  renderer->renderGlyphs = &utfgridRenderGlyphs;

  renderer->getTruetypeTextBBox=&getTruetypeTextBBoxUTFGrid;

  renderer->startLayer=&startNewLayerUTFGrid;
  renderer->endLayer=&closeNewLayerUTFGrid;

  return MS_SUCCESS;
}