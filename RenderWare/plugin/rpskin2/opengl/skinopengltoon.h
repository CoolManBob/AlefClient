/***********************************************************************
 *
 * Module:  skinopengltoon.h
 *
 * Purpose: 
 *
 ***********************************************************************/

#if !defined( SKINOPENGLTOON_H )
#define SKINOPENGLTOON_H

/* =====================================================================
 *  Includes
 * ===================================================================== */


/* =====================================================================
 *  Defines
 * ===================================================================== */


/* =====================================================================
 *  Module specific type definitions
 * ===================================================================== */


/* =====================================================================
 *  Extern variables
 * ===================================================================== */


/* =====================================================================
 *  Extern function prototypes
 * ===================================================================== */

#if defined( __cplusplus )
extern "C"
{
#endif /* defined( __cplusplus ) */

extern void
_rpToonOpenGLAllInOneRenderCB( RwResEntry *repEntry,
                               void *object,
                               const RwUInt8 type,
                               const RwUInt32 flags );

#if defined( __cplusplus )
}
#endif /* defined( __cplusplus ) */

#endif /* !defined( SKINOPENGLTOON_H ) */