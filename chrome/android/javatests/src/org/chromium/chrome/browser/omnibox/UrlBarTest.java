// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox;

import static org.chromium.base.test.util.Restriction.RESTRICTION_TYPE_NON_LOW_END_DEVICE;

import android.annotation.SuppressLint;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.support.test.InstrumentationRegistry;
import android.support.test.filters.SmallTest;
import android.text.Editable;
import android.text.TextUtils;
import android.view.KeyEvent;
import android.view.inputmethod.BaseInputConnection;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CallbackHelper;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.Restriction;
import org.chromium.base.test.util.RetryOnFailure;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.util.OmniboxTestUtils;
import org.chromium.chrome.test.util.OmniboxTestUtils.StubAutocompleteController;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.content.browser.test.util.KeyUtils;

import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Tests for the URL bar UI component.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE,
        ChromeActivityTestRule.DISABLE_NETWORK_PREDICTION_FLAG})
public class UrlBarTest {
    // 9000+ chars of goodness

    @Rule
    public ChromeActivityTestRule<ChromeActivity> mActivityTestRule =
            new ChromeActivityTestRule<>(ChromeActivity.class);

    private static final String HUGE_URL =
            "data:text/plain,H"
            + new String(new char[9000]).replace('\0', 'u')
            + "ge!";

    private UrlBar getUrlBar() {
        return (UrlBar) mActivityTestRule.getActivity().findViewById(R.id.url_bar);
    }

    private void stubLocationBarAutocomplete() {
        setAutocompleteController(new StubAutocompleteController());
    }

    private void setAutocompleteController(final AutocompleteController controller) {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                LocationBarLayout locationBar =
                        (LocationBarLayout) mActivityTestRule.getActivity().findViewById(
                                R.id.location_bar);
                locationBar.cancelPendingAutocompleteStart();
                locationBar.setAutocompleteController(controller);
            }
        });
    }

    private static class AutocompleteState {
        public final boolean hasAutocomplete;
        public final String textWithoutAutocomplete;
        public final String textWithAutocomplete;

        public AutocompleteState(
                boolean hasAutocomplete, String textWithoutAutocomplete,
                String textWithAutocomplete) {
            this.hasAutocomplete = hasAutocomplete;
            this.textWithoutAutocomplete = textWithoutAutocomplete;
            this.textWithAutocomplete = textWithAutocomplete;
        }
    }

    private Editable getUrlBarText(final UrlBar urlBar) throws ExecutionException {
        return ThreadUtils.runOnUiThreadBlocking(new Callable<Editable>() {
            @Override
            public Editable call() throws Exception {
                return urlBar.getText();
            }
        });
    }

    private AutocompleteState getAutocompleteState(
            final UrlBar urlBar, final Runnable action) {
        final AtomicBoolean hasAutocomplete = new AtomicBoolean();
        final AtomicReference<String> textWithoutAutocomplete = new AtomicReference<String>();
        final AtomicReference<String> textWithAutocomplete = new AtomicReference<String>();

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                if (action != null) action.run();
                textWithoutAutocomplete.set(urlBar.getTextWithoutAutocomplete());
                textWithAutocomplete.set(urlBar.getTextWithAutocomplete());
                hasAutocomplete.set(urlBar.hasAutocomplete());
            }
        });

        return new AutocompleteState(
                hasAutocomplete.get(), textWithoutAutocomplete.get(), textWithAutocomplete.get());
    }

    private void setTextAndVerifyNoAutocomplete(final UrlBar urlBar, final String text) {
        AutocompleteState state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.setText(text);
            }
        });

        Assert.assertEquals(text, state.textWithoutAutocomplete);
        Assert.assertEquals(text, state.textWithAutocomplete);
        Assert.assertFalse(state.hasAutocomplete);
    }

    private void setAutocomplete(final UrlBar urlBar,
            final String userText, final String autocompleteText) {
        AutocompleteState state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.setAutocompleteText(userText, autocompleteText);
            }
        });

        Assert.assertEquals(userText, state.textWithoutAutocomplete);
        Assert.assertEquals(userText + autocompleteText, state.textWithAutocomplete);
        Assert.assertTrue(state.hasAutocomplete);
    }

    private AutocompleteState setSelection(
            final UrlBar urlBar, final int selectionStart, final int selectionEnd) {
        return getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.setSelection(selectionStart, selectionEnd);
            }
        });
    }

    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testRefocusing() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();
        UrlBar urlBar = getUrlBar();
        Assert.assertFalse(OmniboxTestUtils.doesUrlBarHaveFocus(urlBar));
        OmniboxTestUtils.checkUrlBarRefocus(urlBar, 5);
    }

    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testAutocompleteUpdatedOnSetText() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();
        stubLocationBarAutocomplete();
        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);

        // Verify that setting a new string will clear the autocomplete.
        setTextAndVerifyNoAutocomplete(urlBar, "test");
        setAutocomplete(urlBar, "test", "ing is fun");
        setTextAndVerifyNoAutocomplete(urlBar, "new string");

        // Replace part of the non-autocomplete text and see that the autocomplete is cleared.
        setTextAndVerifyNoAutocomplete(urlBar, "test");
        setAutocomplete(urlBar, "test", "ing is fun");
        AutocompleteState state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.setText(urlBar.getText().replace(1, 2, "a"));
            }
        });
        Assert.assertFalse(state.hasAutocomplete);
        Assert.assertEquals("tasting is fun", state.textWithoutAutocomplete);
        Assert.assertEquals("tasting is fun", state.textWithAutocomplete);

        // Replace part of the autocomplete text and see that the autocomplete is cleared.
        setTextAndVerifyNoAutocomplete(urlBar, "test");
        setAutocomplete(urlBar, "test", "ing is fun");
        state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.setText(urlBar.getText().replace(8, 10, "no"));
            }
        });
        Assert.assertFalse(state.hasAutocomplete);
        Assert.assertEquals("testing no fun", state.textWithoutAutocomplete);
        Assert.assertEquals("testing no fun", state.textWithAutocomplete);
    }

    private void verifySelectionState(
            String text, String inlineAutocomplete, int selectionStart, int selectionEnd,
            boolean expectedHasAutocomplete, String expectedTextWithoutAutocomplete,
            String expectedTextWithAutocomplete, boolean expectedPreventInline,
            String expectedRequestedAutocompleteText)
                    throws InterruptedException, TimeoutException {
        final UrlBar urlBar = getUrlBar();

        stubLocationBarAutocomplete();
        setTextAndVerifyNoAutocomplete(urlBar, text);
        setAutocomplete(urlBar, text, inlineAutocomplete);

        final CallbackHelper autocompleteHelper = new CallbackHelper();
        final AtomicReference<String> requestedAutocompleteText = new AtomicReference<String>();
        final AtomicBoolean didPreventInlineAutocomplete = new AtomicBoolean();
        final StubAutocompleteController controller = new StubAutocompleteController() {
            @Override
            public void start(Profile profile, String url, String text, int cursorPosition,
                    boolean preventInlineAutocomplete) {
                if (autocompleteHelper.getCallCount() != 0) return;

                requestedAutocompleteText.set(text);
                didPreventInlineAutocomplete.set(preventInlineAutocomplete);
                autocompleteHelper.notifyCalled();
            }
        };
        setAutocompleteController(controller);

        AutocompleteState state = setSelection(urlBar, selectionStart, selectionEnd);
        Assert.assertEquals("Has autocomplete", expectedHasAutocomplete, state.hasAutocomplete);
        Assert.assertEquals("Text w/o Autocomplete", expectedTextWithoutAutocomplete,
                state.textWithoutAutocomplete);
        Assert.assertEquals(
                "Text w/ Autocomplete", expectedTextWithAutocomplete, state.textWithAutocomplete);

        autocompleteHelper.waitForCallback(0);
        Assert.assertEquals("Prevent inline autocomplete", expectedPreventInline,
                didPreventInlineAutocomplete.get());
        Assert.assertEquals("Requested autocomplete text", expectedRequestedAutocompleteText,
                requestedAutocompleteText.get());
    }

    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testAutocompleteUpdatedOnSelection() throws InterruptedException, TimeoutException {
        mActivityTestRule.startMainActivityOnBlankPage();
        stubLocationBarAutocomplete();

        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);

        // Verify that setting a selection before the autocomplete clears it.
        verifySelectionState("test", "ing is fun", 1, 1, false, "test", "test", true, "test");

        // Verify that setting a selection range before the autocomplete clears it.
        verifySelectionState("test", "ing is fun", 0, 4, false, "test", "test", true, "test");

        // Verify that setting a selection at the start of the autocomplete clears it.
        verifySelectionState("test", "ing is fun", 4, 4, false, "test", "test", true, "test");

        // Verify that setting a selection range that covers a portion of the non-autocomplete
        // and autocomplete text does not delete the autocomplete text.
        verifySelectionState("test", "ing is fun", 2, 5,
                false, "testing is fun", "testing is fun", true, "testing is fun");

        // Verify that setting a selection range that over the entire string does not delete
        // the autocomplete text.
        verifySelectionState("test", "ing is fun", 0, 14,
                false, "testing is fun", "testing is fun", true, "testing is fun");

        // Verify that setting a selection at the end of the text does not delete the autocomplete
        // text.
        verifySelectionState("test", "ing is fun", 14, 14,
                false, "testing is fun", "testing is fun", false, "testing is fun");

        // Verify that setting a selection in the middle of the autocomplete text does not delete
        // the autocomplete text.
        verifySelectionState("test", "ing is fun", 9, 9,
                false, "testing is fun", "testing is fun", true, "testing is fun");

        // Verify that setting a selection range in the middle of the autocomplete text does not
        // delete the autocomplete text.
        verifySelectionState("test", "ing is fun", 8, 11,
                false, "testing is fun", "testing is fun", true, "testing is fun");

        // Verify that setting the same selection does not clear the autocomplete text.
        // As we do not expect the suggestions to be refreshed, we test this slightly differently
        // than the other cases.
        stubLocationBarAutocomplete();
        setTextAndVerifyNoAutocomplete(urlBar, "test");
        setAutocomplete(urlBar, "test", "ing is fun");
        AutocompleteState state = setSelection(urlBar, 4, 14);
        Assert.assertEquals("Has autocomplete", true, state.hasAutocomplete);
        Assert.assertEquals("Text w/o Autocomplete", "test", state.textWithoutAutocomplete);
        Assert.assertEquals("Text w/ Autocomplete", "testing is fun", state.textWithAutocomplete);
    }

    /**
     * Ensure that we allow inline autocomplete when the text gets shorter but is not an explicit
     * delete action by the user.
     *
     * If you focus the omnibox and there is the selected text "[about:blank]", then typing new text
     * should clear that entirely and allow autocomplete on the newly entered text.
     *
     * If we assume deletes happen any time the text gets shorter, then this would be prevented.
     */
    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testAutocompleteAllowedWhenReplacingText()
            throws InterruptedException, TimeoutException {
        mActivityTestRule.startMainActivityOnBlankPage();

        final String textToBeEntered = "c";

        final CallbackHelper autocompleteHelper = new CallbackHelper();
        final AtomicBoolean didPreventInlineAutocomplete = new AtomicBoolean();
        final StubAutocompleteController controller = new StubAutocompleteController() {
            @Override
            public void start(Profile profile, String url, String text, int cursorPosition,
                    boolean preventInlineAutocomplete) {
                if (!TextUtils.equals(textToBeEntered, text)) return;
                if (autocompleteHelper.getCallCount() != 0) return;

                didPreventInlineAutocomplete.set(preventInlineAutocomplete);
                autocompleteHelper.notifyCalled();
            }
        };
        setAutocompleteController(controller);

        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                urlBar.beginBatchEdit();
                urlBar.setText(textToBeEntered);
                urlBar.setSelection(textToBeEntered.length());
                urlBar.endBatchEdit();
            }
        });
        autocompleteHelper.waitForCallback(0);
        Assert.assertFalse(
                "Inline autocomplete incorrectly prevented.", didPreventInlineAutocomplete.get());
    }

    /**
     * Ensure that if the user deletes just the inlined autocomplete text that the suggestions are
     * regenerated.
     */
    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testSuggestionsUpdatedWhenDeletingInlineAutocomplete()
            throws InterruptedException, TimeoutException {
        mActivityTestRule.startMainActivityOnBlankPage();

        stubLocationBarAutocomplete();
        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);

        setTextAndVerifyNoAutocomplete(urlBar, "test");
        setAutocomplete(urlBar, "test", "ing");

        final CallbackHelper autocompleteHelper = new CallbackHelper();
        final AtomicBoolean didPreventInlineAutocomplete = new AtomicBoolean();
        final StubAutocompleteController controller = new StubAutocompleteController() {
            @Override
            public void start(Profile profile, String url, String text, int cursorPosition,
                    boolean preventInlineAutocomplete) {
                if (!TextUtils.equals("test", text)) return;
                if (autocompleteHelper.getCallCount() != 0) return;

                didPreventInlineAutocomplete.set(preventInlineAutocomplete);
                autocompleteHelper.notifyCalled();
            }
        };
        setAutocompleteController(controller);

        KeyUtils.singleKeyEventView(
                InstrumentationRegistry.getInstrumentation(), urlBar, KeyEvent.KEYCODE_DEL);

        CriteriaHelper.pollUiThread(Criteria.equals("test", new Callable<String>() {
            @Override
            public String call() throws Exception {
                return urlBar.getText().toString();
            }
        }));

        autocompleteHelper.waitForCallback(0);
        Assert.assertTrue("Inline autocomplete incorrectly allowed after delete.",
                didPreventInlineAutocomplete.get());
    }

    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testSelectionChangesIgnoredInBatchMode() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();
        stubLocationBarAutocomplete();
        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);

        setTextAndVerifyNoAutocomplete(urlBar, "test");
        setAutocomplete(urlBar, "test", "ing is fun");

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                urlBar.beginBatchEdit();
            }
        });
        // Ensure the autocomplete is not modified if in batch mode.
        AutocompleteState state = setSelection(urlBar, 1, 1);
        Assert.assertTrue(state.hasAutocomplete);
        Assert.assertEquals("test", state.textWithoutAutocomplete);
        Assert.assertEquals("testing is fun", state.textWithAutocomplete);
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                urlBar.endBatchEdit();
            }
        });

        // Ensure that after batch mode has ended that the autocomplete is cleared due to the
        // invalid selection range.
        state = getAutocompleteState(urlBar, null);
        Assert.assertFalse(state.hasAutocomplete);
        Assert.assertEquals("test", state.textWithoutAutocomplete);
        Assert.assertEquals("test", state.textWithAutocomplete);
    }

    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testBatchModeChangesTriggerCorrectSuggestions() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();

        final AtomicReference<String> requestedAutocompleteText = new AtomicReference<String>();
        final StubAutocompleteController controller = new StubAutocompleteController() {
            @Override
            public void start(Profile profile, String url, String text, int cursorPosition,
                    boolean preventInlineAutocomplete) {
                requestedAutocompleteText.set(text);
            }
        };

        setAutocompleteController(controller);

        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);

        setTextAndVerifyNoAutocomplete(urlBar, "test");
        setAutocomplete(urlBar, "test", "ing is fun");

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                urlBar.beginBatchEdit();
            }
        });
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                urlBar.getInputConnection().commitText("y", 1);
            }
        });
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                urlBar.endBatchEdit();
            }
        });

        CriteriaHelper.pollUiThread(Criteria.equals("testy", new Callable<String>() {
            @Override
            public String call() {
                return requestedAutocompleteText.get();
            }
        }));
    }

    @Test
    @SmallTest
    @Feature("Omnibox")
    @RetryOnFailure
    public void testAutocompleteCorrectlyPerservedOnBatchMode() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();
        stubLocationBarAutocomplete();

        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);
        OmniboxTestUtils.waitForFocusAndKeyboardActive(urlBar, true);

        // Valid case (cursor at the end of text, single character, matches previous autocomplete).
        setAutocomplete(urlBar, "g", "oogle.com");
        AutocompleteState state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            // TODO(crbug.com/635567): Fix this properly.
            @SuppressLint("SetTextI18n")
            public void run() {
                urlBar.beginBatchEdit();
                urlBar.setText("go");
                urlBar.setSelection(2);
                urlBar.endBatchEdit();
            }
        });
        Assert.assertTrue(state.hasAutocomplete);
        Assert.assertEquals("google.com", state.textWithAutocomplete);
        Assert.assertEquals("go", state.textWithoutAutocomplete);

        // Invalid case (cursor not at the end of the text)
        setAutocomplete(urlBar, "g", "oogle.com");
        state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            // TODO(crbug.com/635567): Fix this properly.
            @SuppressLint("SetTextI18n")
            public void run() {
                urlBar.beginBatchEdit();
                urlBar.setText("go");
                urlBar.setSelection(0);
                urlBar.endBatchEdit();
            }
        });
        Assert.assertFalse(state.hasAutocomplete);

        // Invalid case (next character did not match previous autocomplete)
        setAutocomplete(urlBar, "g", "oogle.com");
        state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            // TODO(crbug.com/635567): Fix this properly.
            @SuppressLint("SetTextI18n")
            public void run() {
                urlBar.beginBatchEdit();
                urlBar.setText("ga");
                urlBar.setSelection(2);
                urlBar.endBatchEdit();
            }
        });
        Assert.assertFalse(state.hasAutocomplete);

        // Invalid case (multiple characters entered instead of 1)
        setAutocomplete(urlBar, "g", "oogle.com");
        state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            // TODO(crbug.com/635567): Fix this properly.
            @SuppressLint("SetTextI18n")
            public void run() {
                urlBar.beginBatchEdit();
                urlBar.setText("googl");
                urlBar.setSelection(5);
                urlBar.endBatchEdit();
            }
        });
        Assert.assertFalse(state.hasAutocomplete);
    }

    @Test
    @SmallTest
    @Feature("Omnibox")
    @RetryOnFailure
    public void testAutocompleteSpanClearedOnNonMatchingCommitText() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();

        stubLocationBarAutocomplete();

        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);
        OmniboxTestUtils.waitForFocusAndKeyboardActive(urlBar, true);

        setTextAndVerifyNoAutocomplete(urlBar, "a");
        setAutocomplete(urlBar, "a", "mazon.com");

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                urlBar.getInputConnection().beginBatchEdit();
                urlBar.getInputConnection().commitText("l", 1);
                urlBar.getInputConnection().setComposingText("", 1);
                urlBar.getInputConnection().endBatchEdit();
            }
        });

        CriteriaHelper.pollUiThread(Criteria.equals("al", new Callable<String>() {
            @Override
            public String call() {
                return urlBar.getText().toString();
            }
        }));
    }

    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testAutocompleteUpdatedOnDefocus() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();
        stubLocationBarAutocomplete();
        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);

        // Verify that defocusing the UrlBar clears the autocomplete.
        setTextAndVerifyNoAutocomplete(urlBar, "test");
        setAutocomplete(urlBar, "test", "ing is fun");
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, false);
        AutocompleteState state = getAutocompleteState(urlBar, null);
        Assert.assertFalse(state.hasAutocomplete);
    }

    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testAutocompleteClearedOnComposition()
            throws InterruptedException, ExecutionException {
        mActivityTestRule.startMainActivityOnBlankPage();
        stubLocationBarAutocomplete();
        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);
        OmniboxTestUtils.waitForFocusAndKeyboardActive(urlBar, true);

        setTextAndVerifyNoAutocomplete(urlBar, "test");
        setAutocomplete(urlBar, "test", "ing is fun");

        Assert.assertNotNull(urlBar.getInputConnection());
        AutocompleteState state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.getInputConnection().setComposingText("ing compose", 4);
            }
        });
        Assert.assertFalse(state.hasAutocomplete);

        Editable urlText = getUrlBarText(urlBar);
        Assert.assertEquals("testing compose", urlText.toString());
        // TODO(tedchoc): Investigate why this fails on x86.
        //assertEquals(4, BaseInputConnection.getComposingSpanStart(urlText));
        //assertEquals(15, BaseInputConnection.getComposingSpanEnd(urlText));
    }

    @Test
    @SmallTest
    @Feature("Omnibox")
    @RetryOnFailure
    @Restriction({RESTRICTION_TYPE_NON_LOW_END_DEVICE}) // crbug.com/635714
    public void testDelayedCompositionCorrectedWithAutocomplete()
            throws InterruptedException, ExecutionException {
        mActivityTestRule.startMainActivityOnBlankPage();
        stubLocationBarAutocomplete();

        final UrlBar urlBar = getUrlBar();
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);
        OmniboxTestUtils.waitForFocusAndKeyboardActive(urlBar, true);

        Assert.assertNotNull(urlBar.getInputConnection());

        // Test with a single autocomplete

        setTextAndVerifyNoAutocomplete(urlBar, "chrome://f");
        setAutocomplete(urlBar, "chrome://f", "lags");

        AutocompleteState state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.getInputConnection().setComposingRegion(13, 14);
                urlBar.getInputConnection().setComposingText("f", 1);
            }
        });
        Assert.assertFalse(state.hasAutocomplete);

        Editable urlText = getUrlBarText(urlBar);
        Assert.assertEquals("chrome://f", urlText.toString());
        Assert.assertEquals(BaseInputConnection.getComposingSpanStart(urlText), 9);
        Assert.assertEquals(BaseInputConnection.getComposingSpanEnd(urlText), 10);

        // Test with > 1 characters in composition.

        setTextAndVerifyNoAutocomplete(urlBar, "chrome://fl");
        setAutocomplete(urlBar, "chrome://fl", "ags");

        state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.getInputConnection().setComposingRegion(12, 14);
                urlBar.getInputConnection().setComposingText("fl", 1);
            }
        });
        Assert.assertFalse(state.hasAutocomplete);

        urlText = getUrlBarText(urlBar);
        Assert.assertEquals("chrome://fl", urlText.toString());
        Assert.assertEquals(BaseInputConnection.getComposingSpanStart(urlText), 9);
        Assert.assertEquals(BaseInputConnection.getComposingSpanEnd(urlText), 11);

        // Test with non-matching composition.  Should just append to the URL text.

        setTextAndVerifyNoAutocomplete(urlBar, "chrome://f");
        setAutocomplete(urlBar, "chrome://f", "lags");

        state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.getInputConnection().setComposingRegion(13, 14);
                urlBar.getInputConnection().setComposingText("g", 1);
            }
        });
        Assert.assertFalse(state.hasAutocomplete);

        urlText = getUrlBarText(urlBar);
        Assert.assertEquals("chrome://fg", urlText.toString());
        Assert.assertEquals(BaseInputConnection.getComposingSpanStart(urlText), 10);
        Assert.assertEquals(BaseInputConnection.getComposingSpanEnd(urlText), 11);

        // Test with composition text that matches the entire text w/o autocomplete.

        setTextAndVerifyNoAutocomplete(urlBar, "chrome://f");
        setAutocomplete(urlBar, "chrome://f", "lags");

        state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.getInputConnection().setComposingRegion(13, 14);
                urlBar.getInputConnection().setComposingText("chrome://f", 1);
            }
        });
        Assert.assertFalse(state.hasAutocomplete);

        urlText = getUrlBarText(urlBar);
        Assert.assertEquals("chrome://f", urlText.toString());
        Assert.assertEquals(BaseInputConnection.getComposingSpanStart(urlText), 0);
        Assert.assertEquals(BaseInputConnection.getComposingSpanEnd(urlText), 10);

        // Test with composition text longer than the URL text.  Shouldn't crash and should
        // just append text.

        setTextAndVerifyNoAutocomplete(urlBar, "chrome://f");
        setAutocomplete(urlBar, "chrome://f", "lags");

        state = getAutocompleteState(urlBar, new Runnable() {
            @Override
            public void run() {
                urlBar.getInputConnection().setComposingRegion(13, 14);
                urlBar.getInputConnection().setComposingText("blahblahblah", 1);
            }
        });
        Assert.assertFalse(state.hasAutocomplete);

        urlText = getUrlBarText(urlBar);
        Assert.assertEquals("chrome://fblahblahblah", urlText.toString());
        Assert.assertEquals(BaseInputConnection.getComposingSpanStart(urlText), 10);
        Assert.assertEquals(BaseInputConnection.getComposingSpanEnd(urlText), 22);
    }

    /**
     * Test to verify the omnibox can take focus during startup before native libraries have
     * loaded.
     */
    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testFocusingOnStartup() {
        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.addCategory(Intent.CATEGORY_LAUNCHER);
        mActivityTestRule.prepareUrlIntent(intent, "about:blank");
        mActivityTestRule.startActivityCompletely(intent);

        UrlBar urlBar = getUrlBar();
        Assert.assertNotNull(urlBar);
        OmniboxTestUtils.toggleUrlBarFocus(urlBar, true);
        OmniboxTestUtils.waitForFocusAndKeyboardActive(urlBar, true);
    }

    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testCopyHuge() throws InterruptedException {
        mActivityTestRule.startMainActivityWithURL(HUGE_URL);
        OmniboxTestUtils.toggleUrlBarFocus(getUrlBar(), true);
        Assert.assertEquals(HUGE_URL, copyUrlToClipboard(android.R.id.copy));
    }

    @Test
    @SmallTest
    @Feature({"Omnibox"})
    @RetryOnFailure
    public void testCutHuge() throws InterruptedException {
        mActivityTestRule.startMainActivityWithURL(HUGE_URL);
        OmniboxTestUtils.toggleUrlBarFocus(getUrlBar(), true);
        Assert.assertEquals(HUGE_URL, copyUrlToClipboard(android.R.id.cut));
    }

    /**
     * Clears the clipboard, executes specified action on the omnibox and
     * returns clipboard's content. Action can be either android.R.id.copy
     * or android.R.id.cut.
     */
    private String copyUrlToClipboard(final int action) {
        return ThreadUtils.runOnUiThreadBlockingNoException(new Callable<String>() {
            @Override
            public String call() {
                ClipboardManager clipboardManager =
                        (ClipboardManager) mActivityTestRule.getActivity().getSystemService(
                                Context.CLIPBOARD_SERVICE);

                clipboardManager.setPrimaryClip(ClipData.newPlainText(null, ""));

                Assert.assertTrue(getUrlBar().onTextContextMenuItem(action));
                ClipData clip = clipboardManager.getPrimaryClip();
                CharSequence text = (clip != null && clip.getItemCount() != 0)
                        ? clip.getItemAt(0).getText()
                        : null;
                return text != null ? text.toString() : null;
            }
        });
    }

    @Before
    public void setUp() throws InterruptedException {
        // Each test will start the activity.
    }
}
