"use strict";

window.OpenAudioPlayer = (() => {
    const $ = (id) =>
        document.getElementById(id);

    let volumeSlider = null;
    let volumeValue = null;
    let muteButton = null;

    let stationName = null;
    let stationInitials = null;
    let trackTitle = null;

    let codecValue = null;
    let bitrateValue = null;
    let sampleRateValue = null;
    let channelValue = null;

    let streamStatus = null;
    let underrunStatus = null;
    let bufferSizeValue = null;
    let bufferStatusValue = null;

    let topbarRssi = null;
    let wifiRssi = null;
    let wifiQuality = null;
    let uptimeValue = null;

    let currentMuted = false;
    let sliderInUse = false;
    let volumeTimer = null;
    let statusTimer = null;

    function createInitials(name) {
        if (!name) {
            return "OA";
        }

        return name
            .trim()
            .split(/\s+/)
            .slice(0, 2)
            .map((word) => word.charAt(0))
            .join("")
            .toUpperCase();
    }

    function formatUptime(totalSeconds) {
        const value =
            Number(totalSeconds) || 0;

        const hours =
            Math.floor(value / 3600);

        const minutes =
            Math.floor((value % 3600) / 60);

        const seconds =
            Math.floor(value % 60);

        return [hours, minutes, seconds]
            .map((part) =>
                String(part).padStart(2, "0")
            )
            .join(":");
    }

    function getWifiQuality(rssi) {
        if (rssi >= -50) {
            return "Sehr gut";
        }

        if (rssi >= -60) {
            return "Gut";
        }

        if (rssi >= -70) {
            return "Ausreichend";
        }

        return "Schwach";
    }

    async function loadStatus() {
        try {
            const status =
                await window.OpenAudioApi.getJson(
                    "/api/status"
                );

            if (stationName) {
                stationName.textContent =
                    status.station ??
                    "Unbekannter Sender";
            }

            if (stationInitials) {
                stationInitials.textContent =
                    createInitials(status.station);
            }

            if (trackTitle) {
                trackTitle.textContent =
                    status.track ||
                    "Keine Titelinformationen";
            }

            if (codecValue) {
                codecValue.textContent =
                    status.codec ?? "–";
            }

            if (bitrateValue) {
                bitrateValue.textContent =
                    `${status.bitrate ?? 0} kbit/s`;
            }

            if (sampleRateValue) {
                const sampleRate =
                    Number(status.sample_rate) || 0;

                sampleRateValue.textContent =
                    `${(sampleRate / 1000)
                        .toLocaleString("de-DE", {
                            minimumFractionDigits: 1,
                            maximumFractionDigits: 1
                        })} kHz`;
            }

            if (channelValue) {
                channelValue.textContent =
                    status.channels === 2
                        ? "Stereo"
                        : `${status.channels ?? 0} Kanal`;
            }

            /*
             * Während der Benutzer den Regler bewegt,
             * darf der Statusabruf den Wert nicht zurücksetzen.
             */
            if (!sliderInUse && volumeSlider) {
                volumeSlider.value =
                    String(status.volume ?? 0);
            }

            if (!sliderInUse && volumeValue) {
                volumeValue.textContent =
                    `${status.volume ?? 0} %`;
            }

            currentMuted =
                Boolean(status.muted);

            if (muteButton) {
                muteButton.textContent =
                    currentMuted ? "🔇" : "🔊";

                muteButton.title =
                    currentMuted
                        ? "Stummschaltung aufheben"
                        : "Stummschalten";
            }

            const rssi =
                Number(status.rssi) || 0;

            const rssiText =
                `${rssi} dBm`;

            if (topbarRssi) {
                topbarRssi.textContent =
                    rssiText;
            }

            if (wifiRssi) {
                wifiRssi.textContent =
                    rssiText;
            }

            if (wifiQuality) {
                wifiQuality.textContent =
                    getWifiQuality(rssi);
            }

            if (uptimeValue) {
                uptimeValue.textContent =
                    formatUptime(status.uptime);
            }

            if (bufferSizeValue) {
                bufferSizeValue.textContent =
                    `${status.buffer_kib ?? 0} KiB`;
            }

            if (bufferStatusValue) {
                bufferStatusValue.textContent =
                    status.playing
                        ? "Aktiv"
                        : "Leer";
            }

            if (streamStatus) {
                streamStatus.textContent =
                    status.playing
                        ? "Verbunden"
                        : "Gestoppt";

                streamStatus.classList.toggle(
                    "status-positive",
                    Boolean(status.playing)
                );
            }

            if (underrunStatus) {
                const underruns =
                    Number(status.underruns) || 0;

                underrunStatus.textContent =
                    underruns === 0
                        ? "Keine Unterläufe"
                        : `${underruns} Unterläufe`;
            }
        } catch (error) {
            console.error(
                "Player-Status konnte nicht geladen werden:",
                error
            );

            if (streamStatus) {
                streamStatus.textContent =
                    "Nicht erreichbar";

                streamStatus.classList.remove(
                    "status-positive"
                );
            }
        }
    }

    function bindVolumeControl() {
        if (!volumeSlider) {
            console.error(
                "volumeSlider wurde im HTML nicht gefunden"
            );

            return;
        }

        volumeSlider.addEventListener(
            "pointerdown",
            () => {
                sliderInUse = true;
            }
        );

        volumeSlider.addEventListener(
            "pointerup",
            () => {
                sliderInUse = false;
            }
        );

        volumeSlider.addEventListener(
            "touchstart",
            () => {
                sliderInUse = true;
            },
            { passive: true }
        );

        volumeSlider.addEventListener(
            "touchend",
            () => {
                sliderInUse = false;
            }
        );

        volumeSlider.addEventListener(
            "input",
            () => {
                const volume =
                    Number(volumeSlider.value);

                sliderInUse = true;

                if (volumeValue) {
                    volumeValue.textContent =
                        `${volume} %`;
                }

                clearTimeout(volumeTimer);

                volumeTimer = window.setTimeout(
                    async () => {
                        try {
                            console.log(
                                "Sende Lautstärke:",
                                volume
                            );

                            const result =
                                await window.OpenAudioApi.postJson(
                                    "/api/player/volume",
                                    { volume }
                                );

                            console.log(
                                "Lautstärke-Antwort:",
                                result
                            );
                        } catch (error) {
                            console.error(
                                "Lautstärke konnte nicht gesetzt werden:",
                                error
                            );
                        } finally {
                            sliderInUse = false;
                        }
                    },
                    120
                );
            }
        );

        /*
         * Für Tastaturbedienung und manche Browser.
         */
        volumeSlider.addEventListener(
            "change",
            async () => {
                const volume =
                    Number(volumeSlider.value);

                try {
                    await window.OpenAudioApi.postJson(
                        "/api/player/volume",
                        { volume }
                    );
                } catch (error) {
                    console.error(
                        "Lautstärke konnte nicht gesetzt werden:",
                        error
                    );
                } finally {
                    sliderInUse = false;
                }
            }
        );
    }

    function bindMuteControl() {
        if (!muteButton) {
            console.error(
                "muteButton wurde im HTML nicht gefunden"
            );

            return;
        }

        muteButton.addEventListener(
            "click",
            async () => {
                const requestedMute =
                    !currentMuted;

                try {
                    console.log(
                        "Sende Mute:",
                        requestedMute
                    );

                    const result =
                        await window.OpenAudioApi.postJson(
                            "/api/player/mute",
                            {
                                muted: requestedMute
                            }
                        );

                    currentMuted =
                        Boolean(result.muted);

                    muteButton.textContent =
                        currentMuted ? "🔇" : "🔊";

                    console.log(
                        "Mute-Antwort:",
                        result
                    );
                } catch (error) {
                    console.error(
                        "Mute konnte nicht gesetzt werden:",
                        error
                    );
                }
            }
        );
    }
function destroy() {
    if (statusTimer !== null) {
        window.clearInterval(statusTimer);
        statusTimer = null;
    }

    if (volumeTimer !== null) {
        window.clearTimeout(volumeTimer);
        volumeTimer = null;
    }
}
    function init() {

    if (statusTimer !== null) {
        window.clearInterval(statusTimer);
        statusTimer = null;
    }
        volumeSlider = $("volumeSlider");
        volumeValue = $("volumeValue");
        muteButton = $("muteButton");

        stationName = $("stationName");
        stationInitials = $("stationInitials");
        trackTitle = $("trackTitle");

        codecValue = $("codecValue");
        bitrateValue = $("bitrateValue");
        sampleRateValue = $("sampleRateValue");
        channelValue = $("channelValue");

        streamStatus = $("streamStatus");
        underrunStatus = $("underrunStatus");
        bufferSizeValue = $("bufferSizeValue");
        bufferStatusValue = $("bufferStatusValue");

        topbarRssi = $("topbarRssi");
        wifiRssi = $("wifiRssi");
        wifiQuality = $("wifiQuality");
        uptimeValue = $("uptimeValue");

        if (!window.OpenAudioApi) {
            console.error(
                "OpenAudioApi ist nicht geladen"
            );

            return;
        }

        bindVolumeControl();
        bindMuteControl();

        loadStatus();

        statusTimer = window.setInterval(
            loadStatus,
            1000
        );

        console.log(
            "OpenAudio32 Player-Modul gestartet"
        );
    }

    return {
        init,
        destroy,
        loadStatus
    };
})();
window.initializePlayerPage = async function () {
    window.OpenAudioPlayer.init();
};