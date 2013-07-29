/******************************************************************************
 * $Id$
 *
 * Project:  MapServer
 * Purpose:  UTFGrid and AGG common ressources
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

#include "renderers/agg/include/agg_path_storage.h"

/*
 * Function that allow vector symbol to be stored in a AGG path storage.
 */
static mapserver::path_storage imageVectorSymbol(symbolObj *symbol)
{
  mapserver::path_storage path;
  int is_new=1;

  for(int i=0; i < symbol->numpoints; i++) {
    if((symbol->points[i].x == -99) && (symbol->points[i].y == -99))
      is_new=1;

    else {
      if(is_new) {
        path.move_to(symbol->points[i].x,symbol->points[i].y);
        is_new=0;
      } else {
        path.line_to(symbol->points[i].x,symbol->points[i].y);
      }
    }
  }
  return path;
}