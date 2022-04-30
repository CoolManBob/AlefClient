/*
 * Interpolating RGBAs with interpolants generated by clipping
 *
 * Copyright (c) Criterion Software Limited
 */
/****************************************************************************
 *                                                                          *
 *  Module  :   nodeRGBAInterp.c                                            *
 *                                                                          *
 *  Purpose :    Uses Interpolants generated by the clipper to generate     *
 *               a set of correctly interpolated RGBAs which are copied into*
 *               the devVerts for resubmission.                             *
 *                                                                          *
 ****************************************************************************/

/****************************************************************************
 Includes
 */

#include "rwcore.h"

#include "rpdbgerr.h"

#include "rtgncpip.h"

/****************************************************************************
 Local Defines
 */

#define MESSAGE(_string)                                             \
    RwDebugSendMessage(rwDEBUGMESSAGE, "nodeRGBAInterpCSL", _string)

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

   Functions

   !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

/*****************************************************************************
 _RGBAInterpNodePipelineNodeInitFn

 Initialises the private data (sets rgba interpolation/overwrite ON)
*/

static              RwBool
_RGBAInterpNodePipelineNodeInitFn(RxPipelineNode * self)
{
    RWFUNCTION(RWSTRING("_RGBAInterpNodePipelineNodeInitFn"));

    if (self)
    {
        NodeRGBAInterpData  data;

        if (RxRenderStateVectorSetDefaultRenderStateVector(&(data.state)) !=
            NULL)
        {
            data.state.DestBlend = rwBLENDONE;
            data.state.SrcBlend = rwBLENDONE;
            /* NOTE: even though we don't *use* vertex alpha, we need to enable "VertexAlpha"
             * on most cards in order to use these non-standard blending modes.
             * It really signifies "we need to read from the framebuffer", not
             * "vertices have alpha != 255"  */
            data.state.Flags |= rxRENDERSTATEFLAG_VERTEXALPHAENABLE;
            data.state.TextureRaster = (RwRaster *)NULL;

            *(NodeRGBAInterpData *) self->privateData = data;

            RWRETURN(TRUE);
        }
        MESSAGE("Failed to initialise default rgba pass render-state");
    }
    RWRETURN(FALSE);
}

/*****************************************************************************
 RGBAInterpNode

 Uses interpolation data generated by the clipper to quickly generate another
 pass of correctly interpolated RGBAs which are then copied into the devVerts
 for resubmission.
*/
static RwBool
_RGBAInterpNode( RxPipelineNodeInstance * self,
                 const RxPipelineNodeParam * params __RWUNUSED__ )
{
    NodeRGBAInterpData  *rgbaInterpData;
    RxPacket            *packet;
    RxCluster           *devVerts;
    RxCluster           *renderState;
    RxCluster           *rxInterps;
    RxCluster           *rxRGBAs;
    RwUInt32             numInterpolants;
    RwInt32              devVertsStride, interpolantsStride, rgbaStride;
    RxInterp            *interpolant;
    RxScrSpace2DVertex  *currentDevVert;
    RxRenderStateVector *rsvp;
    RwRGBAReal          *currentRGBA, *parentRGBA1, *parentRGBA2, rgba;
    RxVertexIndex        currentVert, currentInterpolant, nextInterpedVert;

    RWFUNCTION(RWSTRING("_RGBAInterpNode"));

    packet = (RxPacket *)RxPacketFetch(self);
    RWASSERT(NULL != packet);

    rgbaInterpData = (NodeRGBAInterpData *) self->privateData;
    RWASSERT(NULL != rgbaInterpData);
    if (rgbaInterpData->rgbaInterpOn == FALSE)
    {
        /* Output the packet to the second output of this Node */
        RxPacketDispatch(packet, 1, self);

        RWRETURN(TRUE);
    }

    /* TODO: should use MeshState here (not devverts->numUsed!!)
     * also use it to test ClipFlagsAnd to avoid interpolation
     * and just do copying into devverts (worth it as opposed to
     * first loop disappears with "interps->numUsed == 0" ?
     * Yeah, you swap meshstate (more standard) for interpolants
     * in early-out cases) */

    devVerts = RxClusterLockWrite(packet, 0, self);
    RWASSERT((devVerts != NULL) && (devVerts->numUsed > 0));
    renderState = RxClusterLockWrite(packet, 1, self);
    RWASSERT(renderState != NULL);
    /* If interpolants are absent or contain no data, no triangles were clipped */
    rxInterps = RxClusterLockRead( packet, 2);
    rxRGBAs = RxClusterLockRead( packet, 3);

    if ((NULL != rxRGBAs) && (rxRGBAs->numUsed > 0))
    {
        if (renderState->numAlloced <= 0)
        {
            renderState = RxClusterInitializeData(
                renderState, 1, sizeof(RxRenderStateVector));
            RWASSERT(NULL != renderState);
        }

        rsvp = RxClusterGetCursorData(renderState, RxRenderStateVector);
        RWASSERT(NULL != rsvp);

        /* Set appropriate rendering modes */
       *rsvp = rgbaInterpData->state;
        renderState->numUsed = 1;

        numInterpolants = 0;
        interpolantsStride = 0;
        if ((rxInterps != NULL) && (rxInterps->numUsed > 0))
        {
            numInterpolants = rxInterps->numUsed;
            interpolantsStride = rxInterps->stride;
            /* We'll need to expand the array */
            rxRGBAs = RxClusterLockWrite(packet, 3, self);
            RWASSERT(NULL != rxRGBAs);

            /* Create an extra rgba for every clipping-generated vertex.
             * This is necessary since some final RGBAs are interpolated
             * from the RGBAs of clipping-generated vertices. */
            rxRGBAs = RxClusterResizeData(rxRGBAs, devVerts->numUsed);
            RWASSERT(NULL != rxRGBAs);
        }

        devVertsStride = devVerts->stride;
        rgbaStride = rxRGBAs->stride;

        /* Copy RGBAs in to the devVerts, interpolating those that
         * need to be interpolated */
        currentVert = 0;
        for (currentInterpolant = 0;
             currentInterpolant < numInterpolants;
             currentInterpolant++)
        {
    /* TODO: would this code work unmodified with a line-list?
     *       If so, get nodeClipLine to generate interpolants... */
            interpolant = RxClusterGetCursorData(rxInterps, RxInterp);
            nextInterpedVert = interpolant->originalVert;
            while (currentVert < nextInterpedVert)
            {
                currentRGBA =
                    RxClusterGetCursorData(rxRGBAs, RwRGBAReal);
                currentDevVert =
                    RxClusterGetCursorData(devVerts,
                                           RxScrSpace2DVertex);
                RwIm2DVertexSetRealRGBA(currentDevVert,
                                        currentRGBA->red,
                                        currentRGBA->green,
                                        currentRGBA->blue,
                                        currentRGBA->alpha);
                currentVert++;
                RxClusterIncCursorByStride(rxRGBAs,  rgbaStride);
                RxClusterIncCursorByStride(devVerts, devVertsStride);
            }
            currentRGBA = RxClusterGetCursorData(rxRGBAs, RwRGBAReal);
            currentDevVert =
                RxClusterGetCursorData(devVerts, RxScrSpace2DVertex);
            parentRGBA1 =
                RxClusterGetIndexedData(rxRGBAs, RwRGBAReal,
                                        interpolant->parentVert1);
            parentRGBA2 =
                RxClusterGetIndexedData(rxRGBAs, RwRGBAReal,
                                        interpolant->parentVert2);

            /*
             * rgba.red   = parentRGBA1->red    +
             * interpolant->interp * (parentRGBA2->red   - parentRGBA1->red);
             * rgba.green = parentRGBA1->green  +
             * interpolant->interp * (parentRGBA2->green - parentRGBA1->green);
             * rgba.blue  = parentRGBA1->blue   +
             * interpolant->interp * (parentRGBA2->blue  - parentRGBA1->blue);
             * rgba.alpha = parentRGBA1->alpha  +
             * interpolant->interp * (parentRGBA2->alpha - parentRGBA1->alpha);
             */
            RwRGBARealSub(&rgba, parentRGBA2, parentRGBA1);
            RwRGBARealScale(&rgba, &rgba, interpolant->interp);
            RwRGBARealAdd(&rgba, &rgba, parentRGBA1);

            RwIm2DVertexSetRealRGBA(currentDevVert, rgba.red,
                                    rgba.green, rgba.blue,
                                    rgba.alpha);
            /* Following MUST be done, in case any subsequent vertices interpolate from this one! */
            *currentRGBA = rgba;

            currentVert++;
            RxClusterIncCursorByStride(rxRGBAs,   rgbaStride);
            RxClusterIncCursorByStride(devVerts,  devVertsStride);
            RxClusterIncCursorByStride(rxInterps, interpolantsStride);
        }
        while (currentVert < devVerts->numUsed)
        {
            currentRGBA = RxClusterGetCursorData(rxRGBAs, RwRGBAReal);
            currentDevVert =
                RxClusterGetCursorData(devVerts, RxScrSpace2DVertex);
            RwIm2DVertexSetRealRGBA(currentDevVert, currentRGBA->red,
                                    currentRGBA->green,
                                    currentRGBA->blue,
                                    currentRGBA->alpha);
            currentVert++;
            RxClusterIncCursorByStride(rxRGBAs,  rgbaStride);
            RxClusterIncCursorByStride(devVerts, devVertsStride);
        }

        /* Output the packet to the first output of this Node */
        RxPacketDispatch(packet, 0, self);
    }
    else
    {
        /* Obviously, there were no specular lights around... or the material
         * has zero specularity and the pipe isn't branched to avoid this
         * node in that case - so RGBAs are present but empty, which is
         * perfectly valid */
        RxPacketDispatch(packet, 1, self);
    }

    RWRETURN(TRUE);
}

/**
 * \ingroup rtgencpipe
 * \ref RxNodeDefinitionGetRGBAInterp returns a pointer to a node to
 * fill screen-space vertices with a new set of correctly clipped colors.
 *
 * This node takes an \ref RxCluster holding \ref RwRGBAReal's, which is
 * assumed to be parallel to the packet's screen-space vertices as they
 * were PRIOR to clipping, clips them as if they had been in the vertices
 * during clipping and then overwrites the colors of the screen-space
 * vertices with the resulting values.
 *
 * This node can optionally make use of an \ref RxCluster holding \ref RxInterp
 * values in order to perform its task. This contains information generated
 * by the clipper which allows the node to interpolate subsequent sets of
 * colors. For every vertex generated by the clipper, there is an associated
 * \ref RxInterp value which holds the index of that vertex in the vertex
 * array (hence this process is sensitive to changes in the order of the
 * vertices inbetween clipping and interpolation), its two 'parent' vertices
 * (it was generated by clipping the edge between these two vertices) and the
 * interpolation value (where it is on the original edge between its parent
 * vertices - if it is zero, it is at the first parent vertex, if it is one
 * it is at the other end and values inbetween zero and one are inbetween,
 * linearly distributed). This process will cause the number of elements in
 * the \ref RwRGBAReal cluster to increase by the number of vertices generated
 * by clipping.
 *
 * If this node does not receive any \ref RxInterp values then it will simply
 * copy the color values straight into the screen-space vertices (this
 * case occurs when none of the triangles in the current mesh have been
 * clipped).
 *
 * When the packet is output from this node, after interpolation, the
 * \ref RwRGBAReal cluster's values will be valid (i.e in synch with the post-
 * clipping screen-space vertex cluster that they have been copied into)
 * and so are safe to inspect or reuse further down the pipeline. Note that
 * the colors of offscreen screen-space vertices were never valid and still
 * aren't.
 *
 * This node has a private data struct. This contains a boolean 'rgbaInterpOn'
 * which if set to FALSE will cause the node to effectively be skipped - it
 * will just pass packets on to its second output and do nothing else. This
 * second output will not invalidate the \ref RwRGBAReal or \ref RxInterp
 * clusters, because there may be subsequent nodes (e.g a UV interpolation
 * node as returned by \ref RxNodeDefinitionGetUVInterp) that need them. The
 * private data struct also contains an \ref RxRenderStateVector which is used
 * to overwrite the packet's renderstate, so that the renderstate of each
 * submitted pass of triangles can be set to whatever the user desires
 * (usually in terms of texture and blending modes).
 *
 * The node's second output will also be used if the \ref RwRGBAReal cluster
 * is missing or empty (this may occur if, say, no lights are currently
 * impinging on the object being rendered).
 *
 * \ref RxPipelineNodeReplaceCluster may be used to make this node interpolate
 * any one of many \ref RwRGBAReal clusters, such that multiple instances of
 * this node within a single pipeline can enable you to render objects with
 * more than two textured passes.
 *
 * The node has two outputs. The second output will be used if the rgbaInterpOn
 * \ref RwBool in the node's private data is set to FALSE, or if the \ref RwRGBAReal
 * cluster is missing or empty.
 * The input requirements of this node:
 *
 * \verbatim
   RxClScrSpace2DVertices - required
   RxClRenderState        - don't want
   RxClInterpolants       - optional
   RxClRGBAs              - optional
   \endverbatim
 *
 * The characteristics of this node's first output:
 *
 * \verbatim
   RxClScrSpace2DVertices - valid
   RxClRenderState        - valid
   RxClInterpolants       - no change
   RxClRGBAs              - valid
   \endverbatim
 *
 * The characteristics of this node's second output:
 *
 * \verbatim
   RxClScrSpace2DVertices - no change
   RxClRenderState        - no change
   RxClInterpolants       - no change
   RxClRGBAs              - no change
   \endverbatim
 *
 * This node has a private data struct of the following form:
 *
 * \verbatim
   struct RxNodeRGBAInterpSettings
   {
       RwBool              rgbaInterpOn;
       RxRenderStateVector state;
   };
   \endverbatim
 *
 * The RxInterp struct has the following form:
 * \verbatim

   struct RxInterp
   {
       RxVertexIndex originalVert;
       RxVertexIndex parentVert1, parentVert2;
       RwReal        interp;
   }
   \endverbatim
 *

 * \return A pointer to a node to fill screen-space vertices with a new set
 * of correctly clipped colors on success, or NULL if there is an error
 *
 * \see RxNodeDefinitionGetUVInterp
 * \see RxNodeDefinitionGetClipLine
 * \see RxNodeDefinitionGetClipTriangle
 * \see RxNodeDefinitionGetCullTriangle
 * \see RxNodeDefinitionGetImmInstance
 * \see RxNodeDefinitionGetImmMangleLineIndices
 * \see RxNodeDefinitionGetImmMangleTriangleIndices
 * \see RxNodeDefinitionGetImmRenderSetup
 * \see RxNodeDefinitionGetScatter
 * \see RxNodeDefinitionGetSubmitLine
 * \see RxNodeDefinitionGetSubmitTriangle
 * \see RxNodeDefinitionGetTransform
 *
 */

RxNodeDefinition   *
RxNodeDefinitionGetRGBAInterp(void)
{
    static RxClusterRef N1clofinterest[] = /* */
    { {&RxClScrSpace2DVertices, rxCLALLOWABSENT, rxCLRESERVED},
      {&RxClRenderState, rxCLALLOWABSENT, rxCLRESERVED},
      {&RxClInterpolants, rxCLALLOWABSENT, rxCLRESERVED},
      {&RxClRGBAs, rxCLALLOWABSENT, rxCLRESERVED}
    };

#define NUMCLUSTERSOFINTEREST \
        ((sizeof(N1clofinterest))/(sizeof(N1clofinterest[0])))

    /* TODO: currently RGBAs is set to OPTIONAL so that incoming packets
     * don't have to contain rgba when the node is turned off (just
     * passes the nodes striaght through). There must be a nicer
     * conceit than this... RGBAs are not really OPTIONAL... when
     * the node  *does* anything, they're REQUIRED. I don't know,
     * the whole turning nodes off thing... I don't know...
     * something... not perfect... isdjfio */
    static RxClusterValidityReq N1inputreqs[NUMCLUSTERSOFINTEREST] = /* */
    { rxCLREQ_REQUIRED,
      rxCLREQ_DONTWANT,
      rxCLREQ_OPTIONAL,
      rxCLREQ_OPTIONAL
    };

    static RxClusterValid N1outclRGBAsOut[NUMCLUSTERSOFINTEREST] = /* */
    { rxCLVALID_VALID,
      rxCLVALID_VALID,
      rxCLVALID_NOCHANGE,
      rxCLVALID_VALID
    };

    static RxClusterValid N1outclPassThrough[NUMCLUSTERSOFINTEREST] = /* */
    { rxCLVALID_NOCHANGE,
      rxCLVALID_NOCHANGE,
      rxCLVALID_NOCHANGE,
      rxCLVALID_NOCHANGE
    };

    static RwChar       _RGBAsOut[] = RWSTRING("RGBAsOut");

    static RwChar       _PassThrough[] = RWSTRING("PassThrough");

    static RxOutputSpec N1outputs[] = /* */
    { {_RGBAsOut,
       N1outclRGBAsOut,
       rxCLVALID_NOCHANGE},
      {_PassThrough,
       N1outclPassThrough,
       rxCLVALID_NOCHANGE}
    };

#define NUMOUTPUTS \
        ((sizeof(N1outputs))/(sizeof(N1outputs[0])))

    static RwChar       _RGBAInterp_csl[] = RWSTRING("RGBAInterp.csl");

    static RxNodeDefinition nodeRGBAInterpCSL = /* */
    { _RGBAInterp_csl,
      {_RGBAInterpNode,
       (RxNodeInitFn)NULL,
       (RxNodeTermFn)NULL,
       _RGBAInterpNodePipelineNodeInitFn,
       (RxPipelineNodeTermFn) NULL,
       (RxPipelineNodeConfigFn) NULL,
       (RxConfigMsgHandlerFn) NULL},
      {NUMCLUSTERSOFINTEREST,
       N1clofinterest,
       N1inputreqs,
       NUMOUTPUTS,
       N1outputs},
      /* This is probably a useful/cheap place
       * to set renderstate for a pass */
      sizeof(NodeRGBAInterpData),
      (RxNodeDefEditable) FALSE,
      0
    };

    RxNodeDefinition   *result = &nodeRGBAInterpCSL;

    RWAPIFUNCTION(RWSTRING("RxNodeDefinitionGetRGBAInterp"));

    /*RWMESSAGE((RWSTRING("result %p"), result));*/

    RWRETURN(result);
}
