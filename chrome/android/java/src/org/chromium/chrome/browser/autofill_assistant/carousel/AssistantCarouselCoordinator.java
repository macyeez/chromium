// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill_assistant.carousel;

import android.content.Context;
import android.graphics.Rect;
import android.support.annotation.Nullable;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.util.TypedValue;
import android.view.View;

import org.chromium.chrome.autofill_assistant.R;
import org.chromium.ui.modelutil.ListObservable;
import org.chromium.ui.modelutil.ListObservable.ListObserver;
import org.chromium.ui.modelutil.RecyclerViewAdapter;
import org.chromium.ui.modelutil.SimpleRecyclerViewMcp;

/**
 * Coordinator responsible for suggesting chips to the user.
 */
public class AssistantCarouselCoordinator {
    // TODO(crbug.com/806868): Get those from XML.
    private static final int CHIPS_INNER_SPACING_DP = 16;

    private final LinearLayoutManager mLayoutManager;
    private final RecyclerView mView;

    private boolean mCentered;

    public AssistantCarouselCoordinator(Context context, AssistantCarouselModel model) {
        mLayoutManager = new LinearLayoutManager(
                context, LinearLayoutManager.HORIZONTAL, /* reverseLayout= */ false);
        mView = new RecyclerView(context);
        mView.setLayoutManager(mLayoutManager);
        mView.addItemDecoration(new SpaceItemDecoration(context));
        mView.getItemAnimator().setAddDuration(0);
        mView.getItemAnimator().setChangeDuration(0);
        mView.getItemAnimator().setMoveDuration(0);
        mView.getItemAnimator().setRemoveDuration(0);
        mView.setAdapter(new RecyclerViewAdapter<>(
                new SimpleRecyclerViewMcp<>(model.getChipsModel(), AssistantChip::getType,
                        AssistantChipViewHolder::bind),
                AssistantChipViewHolder::create));

        // Carousel is initially hidden.
        setVisible(false);

        // Listen for changes on REVERSE_LAYOUT.
        model.addObserver((source, propertyKey) -> {
            if (AssistantCarouselModel.ALIGNMENT == propertyKey) {
                switch (model.get(AssistantCarouselModel.ALIGNMENT)) {
                    case AssistantCarouselModel.Alignment.START:
                        mLayoutManager.setReverseLayout(false);
                        mCentered = false;
                        break;
                    case AssistantCarouselModel.Alignment.CENTER:
                        mCentered = true;
                        mView.invalidateItemDecorations();
                        break;
                    case AssistantCarouselModel.Alignment.END:
                        mLayoutManager.setReverseLayout(true);
                        mCentered = false;
                        break;
                }
            } else {
                assert false : "Unhandled property detected in AssistantCarouselCoordinator!";
            }
        });

        // Listen for changes on chips, and set visibility accordingly.
        model.getChipsModel().addObserver(new ListObserver<Void>() {
            @Override
            public void onItemRangeInserted(ListObservable source, int index, int count) {
                onChipsChanged();
            }

            @Override
            public void onItemRangeRemoved(ListObservable source, int index, int count) {
                onChipsChanged();
            }

            @Override
            public void onItemRangeChanged(
                    ListObservable<Void> source, int index, int count, @Nullable Void payload) {
                onChipsChanged();
            }

            private void onChipsChanged() {
                setVisible(model.getChipsModel().size() > 0);
            }
        });
    }

    /**
     * Return the view associated to this carousel.
     */
    public RecyclerView getView() {
        return mView;
    }

    /**
     * Show or hide this carousel within its parent.
     */
    private void setVisible(boolean visible) {
        int visibility = visible ? View.VISIBLE : View.GONE;
        if (mView.getVisibility() != visibility) {
            mView.setVisibility(visibility);
        }
    }

    private class SpaceItemDecoration extends RecyclerView.ItemDecoration {
        private final int mInnerSpacePx;
        private final int mOuterSpacePx;

        SpaceItemDecoration(Context context) {
            mInnerSpacePx = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP,
                    CHIPS_INNER_SPACING_DP / 2, context.getResources().getDisplayMetrics());
            mOuterSpacePx = context.getResources().getDimensionPixelSize(
                    R.dimen.autofill_assistant_bottombar_horizontal_spacing);
        }

        @Override
        public void getItemOffsets(
                Rect outRect, View view, RecyclerView parent, RecyclerView.State state) {
            if (mCentered && parent.getAdapter().getItemCount() == 1) {
                // We have one view and want it horizontally centered. By this time the parent
                // measured width is correct (as it matches its parent), but we need to explicitly
                // measure how big the chip view wants to be.
                int availableWidth = parent.getMeasuredWidth();
                view.measure(
                        View.MeasureSpec.makeMeasureSpec(availableWidth, View.MeasureSpec.AT_MOST),
                        View.MeasureSpec.makeMeasureSpec(
                                parent.getMeasuredHeight(), View.MeasureSpec.AT_MOST));

                int margin = (availableWidth - view.getMeasuredWidth()) / 2;
                outRect.left = margin;
                outRect.right = margin;
                return;
            }

            int position = parent.getChildAdapterPosition(view);
            int left;
            int right;
            if (position == 0) {
                left = mOuterSpacePx;
            } else {
                left = mInnerSpacePx;
            }

            if (position == parent.getAdapter().getItemCount() - 1) {
                right = mOuterSpacePx;
            } else {
                right = mInnerSpacePx;
            }

            if (!mLayoutManager.getReverseLayout()) {
                outRect.left = left;
                outRect.right = right;
            } else {
                outRect.left = right;
                outRect.right = left;
            }
        }
    }
}
