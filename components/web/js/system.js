"use strict";

window.OpenAudioSystem = (() => {
    const $ = (id) =>
        document.getElementById(id);

    let initialized = false;

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

    function bindTabs() {
        const tabs =
            document.querySelectorAll(
                ".system-tab"
            );

        const pages =
            document.querySelectorAll(
                ".system-tab-page"
            );

        tabs.forEach((tab) => {
            tab.addEventListener(
                "click",
                () => {
                    const selected =
                        tab.dataset.systemTab;

                    tabs.forEach((item) => {
                        item.classList.toggle(
                            "active",
                            item === tab
                        );
                    });

                    pages.forEach((page) => {
                        page.classList.toggle(
                            "active",
                            page.dataset.systemTabPage ===
                                selected
                        );
                    });
                }
            );
        });
    }

    function bindRangeDisplays() {
        const bindings = [
            {
                input: "mp3DecodeThresholdInput",
                output: "mp3DecodeThresholdValue",
                suffix: " Byte"
            },
            {
                input: "playbackPrebufferInput",
                output: "playbackPrebufferValue",
                suffix: " ms"
            },
            {
                input: "volumeFadeInput",
                output: "volumeFadeValue",
                suffix: " ms"
            }
        ];

        bindings.forEach((binding) => {
            const input =
                $(binding.input);

            const output =
                $(binding.output);

            if (!input || !output) {
                return;
            }

            const update = () => {
                output.textContent =
                    `${input.value}${binding.suffix}`;
            };

            input.addEventListener(
                "input",
                update
            );

            update();
        });
    }

    function bindBlockDuration() {
        const input =
            $("audioBlockFramesInput");

        const output =
            $("audioBlockDuration");

        if (!input || !output) {
            return;
        }

        const update = () => {
            const frames =
                Number(input.value) || 256;

            const milliseconds =
                frames / 44100 * 1000;

            output.textContent =
                `${milliseconds.toLocaleString(
                    "de-DE",
                    {
                        minimumFractionDigits: 2,
                        maximumFractionDigits: 2
                    }
                )} ms bei 44,1 kHz`;
        };

        input.addEventListener(
            "change",
            update
        );

        update();
    }

    function showSystemMessage(
    text,
    isError = false
) {
    const message =
        $("systemMessage");

    if (!message) {
        return;
    }

    message.textContent =
        text;

    message.classList.remove(
        "hidden"
    );

    message.classList.toggle(
        "error",
        isError
    );

    message.classList.toggle(
        "success",
        !isError
    );
}

function hideSystemMessage() {
    const message =
        $("systemMessage");

    if (!message) {
        return;
    }

    message.classList.add(
        "hidden"
    );

    message.classList.remove(
        "error",
        "success"
    );
}

function updateRestartHint(required) {
    const hint =
        $("systemRestartHint");

    if (!hint) {
        return;
    }

    hint.classList.toggle(
        "hidden",
        !required
    );
}
async function loadSettings() {
    if (!window.OpenAudioApi) {
        console.error(
            "OpenAudioApi ist nicht geladen"
        );

        return;
    }

    try {
        const settings =
            await window.OpenAudioApi.getJson(
                "/api/settings"
            );

        const audioBufferSizeInput =
            $("audioBufferSizeInput");

        const audioBlockFramesInput =
            $("audioBlockFramesInput");

        const mp3DecodeThresholdInput =
            $("mp3DecodeThresholdInput");

        const playbackPrebufferInput =
            $("playbackPrebufferInput");

        const volumeFadeInput =
            $("volumeFadeInput");

        const httpBufferInput =
            $("httpBufferInput");

        const httpTimeoutInput =
            $("httpTimeoutInput");

        const reconnectDelayInput =
            $("reconnectDelayInput");

        const maximumRedirectsInput =
            $("maximumRedirectsInput");

        const underrunLoggingInput =
            $("underrunLoggingInput");

        if (audioBufferSizeInput) {
            audioBufferSizeInput.value =
                String(
                    settings.audio_buffer_kib ??
                    128
                );
        }

        if (audioBlockFramesInput) {
            audioBlockFramesInput.value =
                String(
                    settings.audio_block_frames ??
                    256
                );

            audioBlockFramesInput.dispatchEvent(
                new Event("change")
            );
        }

        if (mp3DecodeThresholdInput) {
            mp3DecodeThresholdInput.value =
                String(
                    settings.mp3_decode_threshold_bytes ??
                    2048
                );

            mp3DecodeThresholdInput.dispatchEvent(
                new Event("input")
            );
        }

        if (playbackPrebufferInput) {
            playbackPrebufferInput.value =
                String(
                    settings.playback_prebuffer_ms ??
                    250
                );

            playbackPrebufferInput.dispatchEvent(
                new Event("input")
            );
        }

        if (volumeFadeInput) {
            volumeFadeInput.value =
                String(
                    settings.volume_fade_ms ??
                    250
                );

            volumeFadeInput.dispatchEvent(
                new Event("input")
            );
        }

        if (httpBufferInput) {
            httpBufferInput.value =
                String(
                    settings.http_buffer_bytes ??
                    4096
                );
        }

        if (httpTimeoutInput) {
            httpTimeoutInput.value =
                String(
                    settings.http_timeout_ms ??
                    15000
                );
        }

        if (reconnectDelayInput) {
            reconnectDelayInput.value =
                String(
                    settings.reconnect_delay_ms ??
                    3000
                );
        }

        if (maximumRedirectsInput) {
            maximumRedirectsInput.value =
                String(
                    settings.maximum_redirects ??
                    5
                );
        }

        if (underrunLoggingInput) {
            underrunLoggingInput.checked =
                Boolean(
                    settings.underrun_logging_enabled
                );
        }

        updateRestartHint(
            Boolean(settings.restart_required)
        );

        console.log(
            "Systemeinstellungen geladen:",
            settings
        );
    } catch (error) {
        console.error(
            "Systemeinstellungen konnten nicht geladen werden:",
            error
        );

        showSystemMessage(
            "Einstellungen konnten nicht geladen werden.",
            true
        );
    }
}
async function saveSettings() {
    if (!window.OpenAudioApi) {
        return;
    }

    const saveButton =
        $("saveAudioSettingsButton");

    const payload = {
        audio_buffer_kib:
            Number(
                $("audioBufferSizeInput")?.value
            ) || 128,

        audio_block_frames:
            Number(
                $("audioBlockFramesInput")?.value
            ) || 256,

        mp3_decode_threshold_bytes:
            Number(
                $("mp3DecodeThresholdInput")?.value
            ) || 2048,

        playback_prebuffer_ms:
            Number(
                $("playbackPrebufferInput")?.value
            ) || 0,

        volume_fade_ms:
            Number(
                $("volumeFadeInput")?.value
            ) || 0,

        http_buffer_bytes:
            Number(
                $("httpBufferInput")?.value
            ) || 4096,

        http_timeout_ms:
            Number(
                $("httpTimeoutInput")?.value
            ) || 15000,

        reconnect_delay_ms:
            Number(
                $("reconnectDelayInput")?.value
            ) || 3000,

        maximum_redirects:
            Number(
                $("maximumRedirectsInput")?.value
            ) || 0,

        underrun_logging_enabled:
            Boolean(
                $("underrunLoggingInput")?.checked
            )
    };

    hideSystemMessage();

    if (saveButton) {
        saveButton.disabled = true;
        saveButton.textContent =
            "Wird gespeichert …";
    }

    try {
        const result =
            await window.OpenAudioApi.postJson(
                "/api/settings",
                payload
            );

        updateRestartHint(
            Boolean(result.restart_required)
        );

        showSystemMessage(
            result.restart_required
                ? "Einstellungen gespeichert. Ein Neustart ist erforderlich."
                : "Einstellungen wurden gespeichert."
        );

        console.log(
            "Systemeinstellungen gespeichert:",
            result
        );
    } catch (error) {
        console.error(
            "Systemeinstellungen konnten nicht gespeichert werden:",
            error
        );

        showSystemMessage(
            "Einstellungen konnten nicht gespeichert werden.",
            true
        );
    } finally {
        if (saveButton) {
            saveButton.disabled = false;
            saveButton.textContent =
                "Einstellungen speichern";
        }
    }
}
    async function loadStatus() {
        if (!window.OpenAudioApi) {
            return;
        }

        try {
            const status =
                await window.OpenAudioApi.getJson(
                    "/api/status"
                );

            const sampleRate =
                Number(status.sample_rate) || 0;

            const underruns =
                Number(status.underruns) || 0;

            const rssi =
                Number(status.rssi) || 0;

            $("systemSampleRate").textContent =
                `${sampleRate} Hz`;

            $("systemUnderruns").textContent =
                String(underruns);

            $("systemUptime").textContent =
                formatUptime(status.uptime);

            $("systemWifiRssi").textContent =
                `${rssi} dBm`;

            $("diagnosticSampleRate").textContent =
                `${sampleRate} Hz`;

            $("diagnosticUnderruns").textContent =
                String(underruns);

            $("diagnosticPlayback").textContent =
                status.playing
                    ? "Aktiv"
                    : "Inaktiv";

            $("diagnosticRssi").textContent =
                `${rssi} dBm`;

            $("diagnosticStation").textContent =
                status.station || "–";
        } catch (error) {
            console.error(
                "Systemstatus konnte nicht geladen werden:",
                error
            );
        }
    }

    function bindButtons() {
    $("saveAudioSettingsButton")
        ?.addEventListener(
            "click",
            saveSettings
        );

    $("systemRestartButton")
    ?.addEventListener(
        "click",
        async () => {
            const confirmed =
                window.confirm(
                    "OpenAudio32 jetzt neu starten?"
                );

            if (!confirmed) {
                return;
            }

            try {
                showSystemMessage(
                    "OpenAudio32 wird neu gestartet …"
                );

                await window.OpenAudioApi.postJson(
                    "/api/system/restart",
                    {}
                );

                window.setTimeout(
                    () => {
                        window.location.reload();
                    },
                    5000
                );
            } catch (error) {
                /*
                 * Beim Neustart kann die Verbindung abbrechen,
                 * obwohl der Neustart korrekt ausgelöst wurde.
                 */
                console.warn(
                    "Verbindung beim Neustart beendet:",
                    error
                );

                showSystemMessage(
                    "Neustart wurde ausgelöst. Verbindung wird wiederhergestellt …"
                );

                window.setTimeout(
                    () => {
                        window.location.reload();
                    },
                    5000
                );
            }
        }
    );

    $("resetUnderrunsButton")
        ?.addEventListener(
            "click",
            () => {
                showSystemMessage(
                    "Das Zurücksetzen des Unterlaufzählers ist noch nicht implementiert.",
                    true
                );
            }
        );

    $("factoryResetButton")
        ?.addEventListener(
            "click",
            () => {
                showSystemMessage(
                    "Werkseinstellungen sind noch nicht implementiert.",
                    true
                );
            }
        );
}

    function init() {
        if (initialized) {
            return;
        }

        initialized = true;

bindTabs();
bindRangeDisplays();
bindBlockDuration();
bindButtons();

loadSettings();
loadStatus();

        window.setInterval(
            loadStatus,
            2000
        );

        console.log(
            "OpenAudio32 System-Modul gestartet"
        );
    }

return {

    init,

    loadStatus,

    loadSettings,

    saveSettings

};
})();
window.initializeSystemPage = async function () {
    window.OpenAudioSystem.init();
};