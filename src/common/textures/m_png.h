#ifndef __M_PNG_H
#define __M_PNG_H
/*
** m_png.h
**
**---------------------------------------------------------------------------
** Copyright 2002-2005 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include <stdio.h>
#include "zstring.h"
#include "files.h"
#include "palentry.h"

// Screenshot buffer image data types
enum ESSType
{
	SS_PAL,
	SS_RGB,
	SS_BGRA
};

// PNG Writing --------------------------------------------------------------

// Start writing an 8-bit palettized PNG file.
// The passed file should be a newly created file.
// This function writes the PNG signature and the IHDR, gAMA, PLTE, and IDAT
// chunks.
bool M_CreatePNG (FileWriter *file, const uint8_t *buffer, const PalEntry *pal,
				  ESSType color_type, int width, int height, int pitch, float gamma);

// Creates a grayscale 1x1 PNG file. Used for savegames without savepics.
bool M_CreateDummyPNG (FileWriter *file);

// Adds a tEXt chunk to a PNG file started with M_CreatePNG.
bool M_AppendPNGText (FileWriter *file, const char *keyword, const char *text);

// Appends the IEND chunk to a PNG file.
bool M_FinishPNG (FileWriter *file);

bool M_SaveBitmap(const uint8_t *from, ESSType color_type, int width, int height, int pitch, FileWriter *file);

// PNG Reading --------------------------------------------------------------

class FBitmap;

bool M_IsPNG(FileSys::FileReader &fr);

bool M_GetPNGSize(FileSys::FileReader &fr, uint32_t &width, uint32_t &height, int32_t *ofs_x = nullptr, int32_t *ofs_y = nullptr, bool *isMasked = nullptr);

bool M_ReadPNG(FileSys::FileReader &&fr, FBitmap * out);

class FGameTexture;

FGameTexture *PNGTexture_CreateFromFile(FileSys::FileReader &&fr, const FString &filename);

#endif
