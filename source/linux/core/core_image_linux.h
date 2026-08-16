// Declarations of the X11 image-module BIFs (implemented in
// core_image_linux.cpp) for use by the LMD function table.
#pragma once

#include "../../abi.h"

BIF_DECL(BIF_Linux_IL_Add);
BIF_DECL(BIF_Linux_IL_Create);
BIF_DECL(BIF_Linux_IL_Destroy);
BIF_DECL(BIF_Linux_ImageSearch);
BIF_DECL(BIF_Linux_LoadPicture);
