// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.geo;

import android.support.annotation.IntDef;

import org.chromium.base.ApiCompatibilityUtils;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.Arrays;
import java.util.Set;

import javax.annotation.Nullable;

/**
 * Visible networks. Stores the data of connected and visible networks.
 */
public class VisibleNetworks {
    private static final String TAG = "VisibleNetworks";

    @Nullable
    private final VisibleWifi mConnectedWifi;
    @Nullable
    private final VisibleCell mConnectedCell;
    @Nullable
    private final Set<VisibleWifi> mAllVisibleWifis;
    @Nullable
    private final Set<VisibleCell> mAllVisibleCells;

    private VisibleNetworks(@Nullable VisibleWifi connectedWifi,
            @Nullable VisibleCell connectedCell, @Nullable Set<VisibleWifi> allVisibleWifis,
            @Nullable Set<VisibleCell> allVisibleCells) {
        this.mConnectedWifi = connectedWifi;
        this.mConnectedCell = connectedCell;
        this.mAllVisibleWifis = allVisibleWifis;
        this.mAllVisibleCells = allVisibleCells;
    }

    public static VisibleNetworks create(@Nullable VisibleWifi connectedWifi,
            @Nullable VisibleCell connectedCell, @Nullable Set<VisibleWifi> allVisibleWifis,
            @Nullable Set<VisibleCell> allVisibleCells) {
        return new VisibleNetworks(connectedWifi, connectedCell, allVisibleWifis, allVisibleCells);
    }

    /**
     * Returns the connected {@link VisibleWifi} or null if the connected wifi is unknown.
     */
    @Nullable
    public VisibleWifi connectedWifi() {
        return mConnectedWifi;
    }

    /**
     * Returns the connected {@link VisibleCell} or null if the connected cell is unknown.
     */
    @Nullable
    public VisibleCell connectedCell() {
        return mConnectedCell;
    }

    /**
     * Returns the current set of {@link VisibleWifi}s that are visible (including not connected
     * networks), or null if the set is unknown.
     */
    @Nullable
    public Set<VisibleWifi> allVisibleWifis() {
        return mAllVisibleWifis;
    }

    /**
     * Returns the current set of {@link VisibleCell}s that are visible (including not connected
     * networks), or null if the set is unknown.
     */
    @Nullable
    public Set<VisibleCell> allVisibleCells() {
        return mAllVisibleCells;
    }

    /**
     * Returns whether this object is empty, meaning there is no visible networks at all.
     */
    public final boolean isEmpty() {
        Set<VisibleWifi> allVisibleWifis = allVisibleWifis();
        Set<VisibleCell> allVisibleCells = allVisibleCells();
        return connectedWifi() == null && connectedCell() == null
                && (allVisibleWifis == null || allVisibleWifis.size() == 0)
                && (allVisibleCells == null || allVisibleCells.size() == 0);
    }

    /**
     * Compares the specified object with this VisibleNetworks for equality.  Returns
     * {@code true} if the given object is a VisibleNetworks and has identical values for
     * all of its fields.
     */
    @Override
    public boolean equals(Object object) {
        if (!(object instanceof VisibleNetworks)) {
            return false;
        }
        VisibleNetworks that = (VisibleNetworks) object;
        return ApiCompatibilityUtils.objectEquals(this.mConnectedWifi, that.connectedWifi())
                && ApiCompatibilityUtils.objectEquals(this.mConnectedCell, that.connectedCell())
                && ApiCompatibilityUtils.objectEquals(this.mAllVisibleWifis, that.allVisibleWifis())
                && ApiCompatibilityUtils.objectEquals(
                           this.mAllVisibleCells, that.allVisibleCells());
    }

    private static int objectsHashCode(Object o) {
        return o != null ? o.hashCode() : 0;
    }

    private static int objectsHash(Object... a) {
        return Arrays.hashCode(a);
    }

    @Override
    public int hashCode() {
        return objectsHash(this.mConnectedWifi, this.mConnectedCell,
                objectsHashCode(this.mAllVisibleWifis), objectsHashCode(this.mAllVisibleCells));
    }

    /**
     * Specification of a visible wifi.
     */
    public static class VisibleWifi {
        public static final VisibleWifi NO_WIFI_INFO = VisibleWifi.create(null, null, null, null);

        @Nullable
        private final String mSsid;
        @Nullable
        private final String mBssid;
        @Nullable
        private final Integer mLevel;
        @Nullable
        private final Long mTimestampMs;

        private VisibleWifi(@Nullable String ssid, @Nullable String bssid, @Nullable Integer level,
                @Nullable Long timestampMs) {
            this.mSsid = ssid;
            this.mBssid = bssid;
            this.mLevel = level;
            this.mTimestampMs = timestampMs;
        }

        public static VisibleWifi create(@Nullable String ssid, @Nullable String bssid,
                @Nullable Integer level, @Nullable Long timestampMs) {
            return new VisibleWifi(ssid, bssid, level, timestampMs);
        }

        /**
         * Returns the SSID of the visible Wifi, or null if unknown.
         */
        @Nullable
        public String ssid() {
            return mSsid;
        }

        /**
         * Returns the BSSID of the visible Wifi, or null if unknown.
         */
        @Nullable
        public String bssid() {
            return mBssid;
        }

        /**
         * Returns the signal level in dBm (RSSI), {@code null} if unknown.
         */
        @Nullable
        public Integer level() {
            return mLevel;
        }

        /**
         * Returns the timestamp in Ms, {@code null} if unknown.
         */
        @Nullable
        public Long timestampMs() {
            return mTimestampMs;
        }

        /**
         * Compares the specified object with this VisibleWifi for equality.  Returns
         * {@code true} if the given object is a VisibleWifi and has identical values for
         * all of its fields except level and timestampMs.
         */
        @Override
        public boolean equals(Object object) {
            if (!(object instanceof VisibleWifi)) {
                return false;
            }

            VisibleWifi that = (VisibleWifi) object;
            return ApiCompatibilityUtils.objectEquals(this.mSsid, that.ssid())
                    && ApiCompatibilityUtils.objectEquals(this.mBssid, that.bssid());
        }

        @Override
        public int hashCode() {
            return VisibleNetworks.objectsHash(this.mSsid, this.mBssid);
        }
    }

    /**
     * Specification of a visible cell.
     */
    public static class VisibleCell {
        public static final VisibleCell UNKNOWN_VISIBLE_CELL =
                VisibleCell.builder(VisibleCell.UNKNOWN_RADIO_TYPE).build();
        public static final VisibleCell UNKNOWN_MISSING_LOCATION_PERMISSION_VISIBLE_CELL =
                VisibleCell.builder(VisibleCell.UNKNOWN_MISSING_LOCATION_PERMISSION_RADIO_TYPE)
                        .build();

        /**
         * Represents all possible values of radio type that we track.
         */
        @Retention(RetentionPolicy.SOURCE)
        @IntDef({UNKNOWN_RADIO_TYPE, UNKNOWN_MISSING_LOCATION_PERMISSION_RADIO_TYPE,
                CDMA_RADIO_TYPE, GSM_RADIO_TYPE, LTE_RADIO_TYPE, WCDMA_RADIO_TYPE})
        public @interface RadioType {}
        public static final int UNKNOWN_RADIO_TYPE = 0;
        public static final int UNKNOWN_MISSING_LOCATION_PERMISSION_RADIO_TYPE = 1;
        public static final int CDMA_RADIO_TYPE = 2;
        public static final int GSM_RADIO_TYPE = 3;
        public static final int LTE_RADIO_TYPE = 4;
        public static final int WCDMA_RADIO_TYPE = 5;

        public static Builder builder(@RadioType int radioType) {
            return new VisibleCell.Builder().setRadioType(radioType);
        }

        @RadioType
        private final int mRadioType;
        @Nullable
        private final Integer mCellId;
        @Nullable
        private final Integer mLocationAreaCode;
        @Nullable
        private final Integer mMobileCountryCode;
        @Nullable
        private final Integer mMobileNetworkCode;
        @Nullable
        private final Integer mPrimaryScramblingCode;
        @Nullable
        private final Integer mPhysicalCellId;
        @Nullable
        private final Integer mTrackingAreaCode;
        @Nullable
        private Long mTimestampMs;

        private VisibleCell(Builder builder) {
            this.mRadioType = builder.mRadioType;
            this.mCellId = builder.mCellId;
            this.mLocationAreaCode = builder.mLocationAreaCode;
            this.mMobileCountryCode = builder.mMobileCountryCode;
            this.mMobileNetworkCode = builder.mMobileNetworkCode;
            this.mPrimaryScramblingCode = builder.mPrimaryScramblingCode;
            this.mPhysicalCellId = builder.mPhysicalCellId;
            this.mTrackingAreaCode = builder.mTrackingAreaCode;
            this.mTimestampMs = builder.mTimestampMs;
        }

        /**
         * Returns the radio type of the visible cell.
         */
        @RadioType
        public int radioType() {
            return mRadioType;
        }

        /**
         * Returns the gsm cell id, {@code null} if unknown.
         */
        @Nullable
        public Integer cellId() {
            return mCellId;
        }

        /**
         * Returns the gsm location area code, {@code null} if unknown.
         */
        @Nullable
        public Integer locationAreaCode() {
            return mLocationAreaCode;
        }

        /**
         * Returns the mobile country code, {@code null} if unknown or GSM.
         */
        @Nullable
        public Integer mobileCountryCode() {
            return mMobileCountryCode;
        }

        /**
         * Returns the mobile network code, {@code null} if unknown or GSM.
         */
        @Nullable
        public Integer mobileNetworkCode() {
            return mMobileNetworkCode;
        }

        /**
         * On a UMTS network, returns the primary scrambling code of the serving cell, {@code null}
         * if unknown or GSM.
         */
        @Nullable
        public Integer primaryScramblingCode() {
            return mPrimaryScramblingCode;
        }

        /**
         * Returns the physical cell id, {@code null} if unknown or not LTE.
         */
        @Nullable
        public Integer physicalCellId() {
            return mPhysicalCellId;
        }

        /**
         * Returns the tracking area code, {@code null} if unknown or not LTE.
         */
        @Nullable
        public Integer trackingAreaCode() {
            return mTrackingAreaCode;
        }

        /**
         * Returns the timestamp in Ms, {@code null} if unknown.
         */
        @Nullable
        public Long timestampMs() {
            return mTimestampMs;
        }

        /**
         * Compares the specified object with this VisibleCell for equality.  Returns
         * {@code true} if the given object is a VisibleWifi and has identical values for
         * all of its fields except timestampMs.
         */
        @Override
        public boolean equals(Object object) {
            if (!(object instanceof VisibleCell)) {
                return false;
            }
            VisibleCell that = (VisibleCell) object;
            return ApiCompatibilityUtils.objectEquals(this.mRadioType, that.radioType())
                    && ApiCompatibilityUtils.objectEquals(this.mCellId, that.cellId())
                    && ApiCompatibilityUtils.objectEquals(
                               this.mLocationAreaCode, that.locationAreaCode())
                    && ApiCompatibilityUtils.objectEquals(
                               this.mMobileCountryCode, that.mobileCountryCode())
                    && ApiCompatibilityUtils.objectEquals(
                               this.mMobileNetworkCode, that.mobileNetworkCode())
                    && ApiCompatibilityUtils.objectEquals(
                               this.mPrimaryScramblingCode, that.primaryScramblingCode())
                    && ApiCompatibilityUtils.objectEquals(
                               this.mPhysicalCellId, that.physicalCellId())
                    && ApiCompatibilityUtils.objectEquals(
                               this.mTrackingAreaCode, that.trackingAreaCode());
        }

        @Override
        public int hashCode() {
            return VisibleNetworks.objectsHash(this.mRadioType, this.mCellId,
                    this.mLocationAreaCode, this.mMobileCountryCode, this.mMobileNetworkCode,
                    this.mPrimaryScramblingCode, this.mPhysicalCellId, this.mTrackingAreaCode);
        }

        /**
         * A {@link VisibleCell} builder.
         */
        public static class Builder {
            @RadioType
            private int mRadioType;
            @Nullable
            private Integer mCellId;
            @Nullable
            private Integer mLocationAreaCode;
            @Nullable
            private Integer mMobileCountryCode;
            @Nullable
            private Integer mMobileNetworkCode;
            @Nullable
            private Integer mPrimaryScramblingCode;
            @Nullable
            private Integer mPhysicalCellId;
            @Nullable
            private Integer mTrackingAreaCode;
            @Nullable
            private Long mTimestampMs;

            public Builder setRadioType(@RadioType int radioType) {
                mRadioType = radioType;
                return this;
            }

            public Builder setCellId(@Nullable Integer cellId) {
                mCellId = cellId;
                return this;
            }

            public Builder setLocationAreaCode(@Nullable Integer locationAreaCode) {
                mLocationAreaCode = locationAreaCode;
                return this;
            }

            public Builder setMobileCountryCode(@Nullable Integer mobileCountryCode) {
                mMobileCountryCode = mobileCountryCode;
                return this;
            }

            public Builder setMobileNetworkCode(@Nullable Integer mobileNetworkCode) {
                mMobileNetworkCode = mobileNetworkCode;
                return this;
            }

            public Builder setPrimaryScramblingCode(@Nullable Integer primaryScramblingCode) {
                mPrimaryScramblingCode = primaryScramblingCode;
                return this;
            }

            public Builder setPhysicalCellId(@Nullable Integer physicalCellId) {
                mPhysicalCellId = physicalCellId;
                return this;
            }

            public Builder setTrackingAreaCode(@Nullable Integer trackingAreaCode) {
                mTrackingAreaCode = trackingAreaCode;
                return this;
            }

            public Builder setTimestamp(@Nullable Long timestampMs) {
                mTimestampMs = timestampMs;
                return this;
            }

            public VisibleCell build() {
                return new VisibleCell(this);
            }
        }
    }
}