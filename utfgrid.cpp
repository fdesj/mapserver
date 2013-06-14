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

/*
 * Generic function to tell the underline device that shape
 * drawing is stating
 */
imageObj *createImageUTFGRID(int width, int height, outputFormatObj *format, colorObj* bg)
{
  imageObj *img = NULL;
  char *charPtr = "Success";

  img = (imageObj *) calloc(1, sizeof (imageObj));
  MS_CHECK_ALLOC(img, sizeof (imageObj), NULL);

  img->img.plugin = (void *) charPtr;
  return img;
}

/*
 * Generic function to tell the underline device that shape
 * drawing is stating
 */
int saveImageUTFGRID(imageObj *img, mapObj *map, FILE *fp, outputFormatObj *format)
{
	return MS_SUCCESS;
}

/*
 * Render polygon shapes with UTFGRID
 *
 */
int renderPolygonUTFGRID(imageObj *img, shapeObj *p, colorObj *color)
{
	return MS_SUCCESS;
}

/*
 * Incase of error, free the memory used by image
 * 
 */
int freeImageUTFGRID(imageObj *img)
{
	return MS_SUCCESS;
}

int getTruetypeTextBBoxUTFGRID(rendererVTableObj *renderer, char **fonts, int numfonts, double size, char *string, rectObj *rect, double **advances,int bAdjustBaseline)
{
	return MS_SUCCESS;
}

int startNewLayerUTFGRID(imageObj *img, mapObj *map, layerObj *layer)
{
	return MS_SUCCESS;
}

int closeNewLayerUTFGRID(imageObj *img, mapObj *map, layerObj *layer)
{
	return MS_SUCCESS;
}

/*
 * Fills the driver Vtable
 * 
 */
int msPopulateRendererVTableUTFGRID( rendererVTableObj *renderer )
{
  renderer->createImage=&createImageUTFGRID;
  renderer->saveImage=&saveImageUTFGRID;
  renderer->renderPolygon=&renderPolygonUTFGRID;
  renderer->freeImage=&freeImageUTFGRID;
  renderer->getTruetypeTextBBox = &getTruetypeTextBBoxUTFGRID;
  renderer->startLayer = &startNewLayerUTFGRID;
  renderer->endLayer = &closeNewLayerUTFGRID;

  return MS_SUCCESS;
}