/*
** autostart.cpp
** This file contains the heads of lists stored in special data segments
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
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
** The particular scheme used here was chosen because it's small.
**
** An alternative that will work with any C++ compiler is to use static
** classes to build these lists at run time. Under Visual C++, doing things
** that way can require a lot of extra space, which is why I'm doing things
** this way.
**
** In the case of PClass lists (section creg), I orginally used the
** constructor to do just that, and the code for that still exists if you
** compile with something other than Visual C++ or GCC.
*/

#include "autosegs.h"

FAutoSeg<AFuncDesc> AutoSegs::ActionFunctons;
FAutoSeg<FieldDesc> AutoSegs::ClassFields;
FAutoSeg<ClassReg> AutoSegs::TypeInfos;
FAutoSeg<FPropertyInfo> AutoSegs::Properties;
FAutoSeg<FMapOptInfo> AutoSegs::MapInfoOptions;
FAutoSeg<FCVarDecl> AutoSegs::CVarDecl;