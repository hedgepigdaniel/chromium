// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the shared code for the new (Chrome 37) security interstitials. It is
// used for both SSL interstitials and Safe Browsing interstitials.

var expandedDetails = false;
var keyPressState = 0;

/**
 * This allows errors to be skippped by typing a secret phrase into the page.
 * @param {string} e The key that was just pressed.
 */
function handleKeypress(e) {
  var BYPASS_SEQUENCE = 'badidea';
  if (BYPASS_SEQUENCE.charCodeAt(keyPressState) == e.keyCode) {
    keyPressState++;
    if (keyPressState == BYPASS_SEQUENCE.length) {
      sendCommand(SecurityInterstitialCommandId.CMD_PROCEED);
      keyPressState = 0;
    }
  } else {
    keyPressState = 0;
  }
}

/**
 * This appends a piece of debugging information to the end of the warning.
 * When complete, the caller must also make the debugging div
 * (error-debugging-info) visible.
 * @param {string} title  The name of this debugging field.
 * @param {string} value  The value of the debugging field.
 * @param {boolean=} fixedWidth If true, the value field is displayed fixed
 *                              width.
 */
function appendDebuggingField(title, value, fixedWidth) {
  // The values input here are not trusted. Never use innerHTML on these
  // values!
  var spanTitle = document.createElement('span');
  spanTitle.classList.add('debugging-title');
  spanTitle.innerText = title + ': ';

  var spanValue = document.createElement('span');
  spanValue.classList.add('debugging-content');
  if (fixedWidth) {
    spanValue.classList.add('debugging-content-fixed-width');
  }
  spanValue.innerText = value;

  var pElem = document.createElement('p');
  pElem.classList.add('debugging-content');
  pElem.appendChild(spanTitle);
  pElem.appendChild(spanValue);
  $('error-debugging-info').appendChild(pElem);
}

function toggleDebuggingInfo() {
  $('error-debugging-info').classList.toggle('hidden');
}

function setupEvents() {
  var overridable = loadTimeData.getBoolean('overridable');
  var interstitialType = loadTimeData.getString('type');
  var ssl = interstitialType == 'SSL';
  var captivePortal = interstitialType == 'CAPTIVE_PORTAL';
  var badClock = ssl && loadTimeData.getBoolean('bad_clock');
  var hidePrimaryButton = loadTimeData.getBoolean('hide_primary_button');

  if (ssl) {
    $('body').classList.add(badClock ? 'bad-clock' : 'ssl');
    $('error-code').textContent = loadTimeData.getString('errorCode');
    $('error-code').classList.remove('hidden');
  } else if (captivePortal) {
    $('body').classList.add('captive-portal');
  } else {
    $('body').classList.add('safe-browsing');
  }

  $('icon').classList.add('icon');

  if (hidePrimaryButton) {
    $('primary-button').classList.add('hidden');
  } else {
    $('primary-button').addEventListener('click', function() {
      switch (interstitialType) {
        case 'CAPTIVE_PORTAL':
          sendCommand(SecurityInterstitialCommandId.CMD_OPEN_LOGIN);
          break;

        case 'SSL':
          if (badClock)
            sendCommand(SecurityInterstitialCommandId.CMD_OPEN_DATE_SETTINGS);
          else if (overridable)
            sendCommand(SecurityInterstitialCommandId.CMD_DONT_PROCEED);
          else
            sendCommand(SecurityInterstitialCommandId.CMD_RELOAD);
          break;

        case 'SAFEBROWSING':
          sendCommand(SecurityInterstitialCommandId.CMD_DONT_PROCEED);
          break;

        default:
          throw 'Invalid interstitial type';
      }
    });
  }

  if (overridable) {
    // Captive portal page isn't overridable.
    $('proceed-link').addEventListener('click', function(event) {
      sendCommand(SecurityInterstitialCommandId.CMD_PROCEED);
    });
  } else if (!ssl) {
    $('final-paragraph').classList.add('hidden');
  }

  if (ssl && overridable) {
    $('proceed-link').classList.add('small-link');
  }

  if ($('diagnostic-link')) {
    $('diagnostic-link').addEventListener('click', function(event) {
      sendCommand(SecurityInterstitialCommandId.CMD_OPEN_DIAGNOSTIC);
    });
  }

  if ($('learn-more-link')) {
    $('learn-more-link').addEventListener('click', function(event) {
      sendCommand(SecurityInterstitialCommandId.CMD_OPEN_HELP_CENTER);
    });
  }

  if (captivePortal) {
    // Captive portal page doesn't have details button.
    $('details-button').classList.add('hidden');
  } else {
    $('details-button').addEventListener('click', function(event) {
      var hiddenDetails = $('details').classList.toggle('hidden');

      if (mobileNav) {
        // Details appear over the main content on small screens.
        $('main-content').classList.toggle('hidden', !hiddenDetails);
      } else {
        $('main-content').classList.remove('hidden');
      }

      $('details-button').innerText = hiddenDetails ?
          loadTimeData.getString('openDetails') :
          loadTimeData.getString('closeDetails');
      if (!expandedDetails) {
        // Record a histogram entry only the first time that details is opened.
        sendCommand(SecurityInterstitialCommandId.CMD_SHOW_MORE_SECTION);
        expandedDetails = true;
      }
    });
  }

  // TODO(felt): This should be simplified once the Finch trial is no longer
  // needed.
  if (interstitialType == 'SAFEBROWSING' &&
      loadTimeData.getBoolean('phishing') && $('report-error-link')) {
    $('report-error-link').addEventListener('click', function(event) {
      sendCommand(SecurityInterstitialCommandId.CMD_REPORT_PHISHING_ERROR);
    });
  }

  preventDefaultOnPoundLinkClicks();
  setupExtendedReportingCheckbox();
  setupSSLDebuggingInfo();
  document.addEventListener('keypress', handleKeypress);
}

document.addEventListener('DOMContentLoaded', setupEvents);
