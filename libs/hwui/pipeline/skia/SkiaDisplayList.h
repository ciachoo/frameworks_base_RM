/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "DisplayList.h"
#include "GLFunctorDrawable.h"
#include "RenderNodeDrawable.h"

#include <deque>
#include <SkLiteDL.h>
#include <SkLiteRecorder.h>

namespace android {
namespace uirenderer {

class Outline;

namespace skiapipeline {

/**
 * This class is intended to be self contained, but still subclasses from
 * DisplayList to make it easier to support switching between the two at
 * runtime.  The downside of this inheritance is that we pay for the overhead
 * of the parent class construction/destruction without any real benefit.
 */
class SkiaDisplayList : public DisplayList {
public:
    SkiaDisplayList() { SkASSERT(projectionReceiveIndex == -1); }
    virtual ~SkiaDisplayList() {
        /* Given that we are using a LinearStdAllocator to store some of the
         * SkDrawable contents we must ensure that any other object that is
         * holding a reference to those drawables is destroyed prior to their
         * deletion.
         */
        mDisplayList.reset();
    }

    /**
     * This resets the DisplayList so that it behaves as if the object were newly
     * constructed.  The reuse avoids any overhead associated with destroying
     * the SkLiteDL as well as the deques and vectors.
     */
    void reset();

    /**
     * Use the linear allocator to create any SkDrawables needed by the display
     * list. This could be dangerous as these objects are ref-counted, so we
     * need to monitor that they don't extend beyond the lifetime of the class
     * that creates them.
     */
    template<class T, typename... Params>
    SkDrawable* allocateDrawable(Params&&... params) {
        return allocator.create<T>(std::forward<Params>(params)...);
    }

    bool isSkiaDL() const override { return true; }

    /**
     * Returns true if the DisplayList does not have any recorded content
     */
    bool isEmpty() const override { return mDisplayList.empty(); }

    /**
     * Returns true if this list directly contains a GLFunctor drawing command.
     */
    bool hasFunctor() const override { return !mChildFunctors.empty(); }

    /**
     * Returns true if this list directly contains a VectorDrawable drawing command.
     */
    bool hasVectorDrawables() const override { return !mVectorDrawables.empty(); }

    /**
     * Attempts to reset and reuse this DisplayList.
     *
     * @return true if the displayList will be reused and therefore should not be deleted
     */
    bool reuseDisplayList(RenderNode* node, renderthread::CanvasContext* context) override;

    /**
     * ONLY to be called by RenderNode::syncDisplayList so that we can notify any
     * contained VectorDrawables or GLFunctors to sync their state.
     *
     * NOTE: This function can be folded into RenderNode when we no longer need
     *       to subclass from DisplayList
     */
    void syncContents() override;

    /**
     * ONLY to be called by RenderNode::prepareTree in order to prepare this
     * list while the UI thread is blocked.  Here we can upload mutable bitmaps
     * and notify our parent if any of our content has been invalidated and in
     * need of a redraw.  If the renderNode has any children then they are also
     * call in order to prepare them.
     *
     * @return true if any content change requires the node to be invalidated
     *
     * NOTE: This function can be folded into RenderNode when we no longer need
     *       to subclass from DisplayList
     */

    bool prepareListAndChildren(TreeObserver& observer, TreeInfo& info, bool functorsNeedLayer,
            std::function<void(RenderNode*, TreeObserver&, TreeInfo&, bool)> childFn) override;

    /**
     *  Calls the provided function once for each child of this DisplayList
     */
    void updateChildren(std::function<void(RenderNode*)> updateFn) override;

    /**
     *  Returns true if there is a child render node that is a projection receiver.
     */
    inline bool containsProjectionReceiver() const { return mProjectionReceiver; }

    void attachRecorder(SkLiteRecorder* recorder, const SkIRect& bounds) {
        recorder->reset(&mDisplayList, bounds);
    }

    void draw(SkCanvas* canvas) { mDisplayList.draw(canvas); }

    void output(std::ostream& output, uint32_t level) override;

    /**
     * We use std::deque here because (1) we need to iterate through these
     * elements and (2) mDisplayList holds pointers to the elements, so they
     * cannot relocate.
     */
    std::deque<RenderNodeDrawable> mChildNodes;
    std::deque<GLFunctorDrawable> mChildFunctors;
    std::vector<SkImage*> mMutableImages;
    std::vector<VectorDrawableRoot*> mVectorDrawables;
    SkLiteDL mDisplayList;

    //mProjectionReceiver points to a child node (stored in mChildNodes) that is as a projection
    //receiver. It is set at record time and used at both prepare and draw tree traversals to
    //make sure backward projected nodes are found and drawn immediately after mProjectionReceiver.
    RenderNodeDrawable* mProjectionReceiver = nullptr;

    //mProjectedOutline is valid only when render node tree is traversed during the draw pass.
    //Render nodes that have a child receiver node, will store a pointer to their outline in
    //mProjectedOutline. Child receiver node will apply the clip before any backward projected
    //node is drawn.
    const Outline* mProjectedOutline = nullptr;

    //mProjectedReceiverParentMatrix is valid when render node tree is traversed during the draw
    //pass. Render nodes that have a child receiver node, will store their matrix in
    //mProjectedReceiverParentMatrix. Child receiver node will set the matrix and then clip with the
    //outline of their parent.
    SkMatrix mProjectedReceiverParentMatrix;
};

}; // namespace skiapipeline
}; // namespace uirenderer
}; // namespace android
