/*****************************************************************************

    File: crease.h

    Purpose: A short description of the file.

    Copyright (c) 2002 Criterion Software Ltd.

 */

#ifndef CREASE_H
#define CREASE_H

/*****************************************************************************
 Includes
 */

/*****************************************************************************
 Defines
 */

/*****************************************************************************
 Enums
 */

/*****************************************************************************
 Typedef Enums
 */

/*****************************************************************************
 Typedef Structs
 */

/*****************************************************************************
 Function Pointers
 */

/*****************************************************************************
 Structs
 */

#ifdef     __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/*****************************************************************************
 Global Variables
 */

/*****************************************************************************
 Function prototypes
 */
void _rpToonCreaseRender(RpToonGeo *toonGeo,
                         const RwV3d *verts,
                         const RwMatrix *transform);

#ifdef    __cplusplus
}
#endif /* __cplusplus */

#endif /* CREASE_H */