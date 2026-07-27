"use strict";

window.OpenAudioAudio = (() => {
    const frequencies10 = [
        31,
        62,
        125,
        250,
        500,
        1000,
        2000,
        4000,
        8000,
        16000
    ];

    const frequencies5 = [
        60,
        230,
        910,
        3600,
        14000
    ];

    const presets = {
        flat: [
            0, 0, 0, 0, 0,
            0, 0, 0, 0, 0
        ],

        bass: [
            6, 5, 4, 2, 1,
            0, 0, 0, 0, 0
        ],

        treble: [
            0, 0, 0, 0, 1,
            2, 3, 4, 5, 5
        ],

        vocal: [
            -2, -1, 0, 2, 4,
            4, 3, 1, 0, -1
        ],

        rock: [
            4, 3, 2, 0, -1,
            1, 3, 4, 4, 3
        ],

        electronic: [
            5, 4, 1, 0, -2,
            1, 3, 5, 5, 4
        ],

        night: [
            -4, -3, -1, 1, 3,
            3, 2, 0, -2, -3
        ]
    };

    let initialized = false;
    let changed = false;
    let currentSettings = null;
    let equalizerValues = [...presets.flat];

    const element = (id) =>
        document.getElementById(id);

    function formatFrequency(frequency) {
        if (frequency >= 1000) {
            const value = frequency / 1000;

            return Number.isInteger(value)
                ? `${value} kHz`
                : `${value.toFixed(1)} kHz`;
        }

        return `${frequency} Hz`;
    }

    function formatDb(value) {
        const numericValue = Number(value);

        if (numericValue > 0) {
            return `+${numericValue} dB`;
        }

        return `${numericValue} dB`;
    }

    function setChanged(value = true) {
        changed = value;

        element("audioUnsavedHint")
            ?.classList.toggle(
                "hidden",
                !changed
            );
    }

    function showMessage(message, type = "success") {
        const messageElement =
            element("audioMessage");

        if (!messageElement) {
            return;
        }

        messageElement.textContent = message;
        messageElement.className =
            `inline-message ${type}`;

        window.setTimeout(() => {
            messageElement.classList.add("hidden");
        }, 5000);
    }

    function updateDspStatus() {
        const statusElement =
            element("audioDspStatus");

        const enabled =
            Boolean(
                element("equalizerEnabledInput")
                    ?.checked
            );

        if (!statusElement) {
            return;
        }

        statusElement.textContent =
            enabled
                ? "DSP aktiviert"
                : "DSP deaktiviert";

        statusElement.className =
            enabled
                ? "audio-status-badge active"
                : "audio-status-badge inactive";
    }

    function getActiveFrequencies() {
        const mode = Number(
            element("equalizerModeInput")
                ?.value ?? 10
        );

        return mode === 5
            ? frequencies5
            : frequencies10;
    }

    function valuesForMode() {
        const mode = Number(
            element("equalizerModeInput")
                ?.value ?? 10
        );

        if (mode === 10) {
            return [...equalizerValues];
        }

        return [
            equalizerValues[1],
            equalizerValues[3],
            equalizerValues[5],
            equalizerValues[7],
            equalizerValues[9]
        ];
    }

    function updateValuesFromFiveBand(values) {
        equalizerValues = [
            values[0],
            values[0],
            values[1],
            values[1],
            values[2],
            values[2],
            values[3],
            values[3],
            values[4],
            values[4]
        ];
    }

    function renderEqualizer() {
        const container =
            element("equalizerBands");

        if (!container) {
            return;
        }

        const frequencies =
            getActiveFrequencies();

        const values =
            valuesForMode();

        container.innerHTML = "";
        container.classList.toggle(
            "mode-5",
            frequencies.length === 5
        );

        frequencies.forEach(
            (frequency, index) => {
                const band =
                    document.createElement("div");

                band.className =
                    "equalizer-band";

                band.innerHTML = `
                    <strong
                        class="equalizer-value"
                        data-eq-value="${index}"
                    >
                        ${formatDb(values[index])}
                    </strong>

                    <div class="equalizer-slider-wrapper">
                        <input
                            class="equalizer-slider"
                            data-eq-slider="${index}"
                            type="range"
                            min="-12"
                            max="12"
                            step="1"
                            value="${values[index]}"
                            aria-label="${formatFrequency(
                                frequency
                            )}"
                        >
                    </div>

                    <span class="equalizer-frequency">
                        ${formatFrequency(frequency)}
                    </span>
                `;

                const slider =
                    band.querySelector(
                        "[data-eq-slider]"
                    );

                slider.addEventListener(
                    "input",
                    () => {
                        const newValue =
                            Number(slider.value);

                        const output =
                            band.querySelector(
                                "[data-eq-value]"
                            );

                        if (output) {
                            output.textContent =
                                formatDb(newValue);
                        }

                        if (frequencies.length === 5) {
                            const fiveBandValues =
                                Array.from(
                                    container.querySelectorAll(
                                        "[data-eq-slider]"
                                    )
                                ).map(
                                    (input) =>
                                        Number(input.value)
                                );

                            updateValuesFromFiveBand(
                                fiveBandValues
                            );
                        } else {
                            equalizerValues[index] =
                                newValue;
                        }

                        const preset =
                            element(
                                "equalizerPresetInput"
                            );

                        if (preset) {
                            preset.value = "custom";
                        }

                        setChanged();
                    }
                );

                container.appendChild(band);
            }
        );
    }

    function setRangeOutput(
        inputId,
        outputId,
        formatter
    ) {
        const input = element(inputId);
        const output = element(outputId);

        if (!input || !output) {
            return;
        }

        const update = () => {
            output.textContent =
                formatter(Number(input.value));
        };

        input.addEventListener(
            "input",
            () => {
                update();
                setChanged();
            }
        );

        update();
    }

    function updateBalanceOutput(value) {
        if (value === 0) {
            return "Mitte";
        }

        if (value < 0) {
            return `${Math.abs(value)} % links`;
        }

        return `${value} % rechts`;
    }

    function applyPreset(presetName) {
        if (!presets[presetName]) {
            return;
        }

        equalizerValues =
            [...presets[presetName]];

        renderEqualizer();
        setChanged();
    }

    function collectSettings() {
        return {
            equalizer_enabled:
                Boolean(
                    element("equalizerEnabledInput")
                        ?.checked
                ),

            equalizer_mode:
                Number(
                    element("equalizerModeInput")
                        ?.value ?? 10
                ),

            preset:
                element("equalizerPresetInput")
                    ?.value ?? "custom",

            bands:
                [...equalizerValues],

            bass:
                Number(
                    element("bassInput")
                        ?.value ?? 0
                ),

            treble:
                Number(
                    element("trebleInput")
                        ?.value ?? 0
                ),

            balance:
                Number(
                    element("balanceInput")
                        ?.value ?? 0
                ),

            loudness_enabled:
                Boolean(
                    element("loudnessEnabledInput")
                        ?.checked
                ),

            limiter_enabled:
                Boolean(
                    element("limiterEnabledInput")
                        ?.checked
                ),

            limiter_threshold_db:
                Number(
                    element("limiterThresholdInput")
                        ?.value ?? -1
                ),

            startup_volume:
                Number(
                    element("startupVolumeInput")
                        ?.value ?? 20
                ),

            maximum_volume:
                Number(
                    element("maximumVolumeInput")
                        ?.value ?? 100
                ),

            output_mode:
                element("outputModeInput")
                    ?.value ?? "stereo",

            swap_channels:
                Boolean(
                    element("swapChannelsInput")
                        ?.checked
                )
        };
    }

    function applySettings(settings) {
        currentSettings = settings;

        equalizerValues =
            Array.isArray(settings.bands) &&
            settings.bands.length === 10
                ? settings.bands.map(Number)
                : [...presets.flat];

        const assignments = {
            equalizerEnabledInput:
                Boolean(settings.equalizer_enabled),

            equalizerModeInput:
                Number(settings.equalizer_mode ?? 10),

            equalizerPresetInput:
                settings.preset ?? "custom",

            bassInput:
                Number(settings.bass ?? 0),

            trebleInput:
                Number(settings.treble ?? 0),

            balanceInput:
                Number(settings.balance ?? 0),

            loudnessEnabledInput:
                Boolean(settings.loudness_enabled),

            limiterEnabledInput:
                settings.limiter_enabled !== false,

            limiterThresholdInput:
                Number(
                    settings.limiter_threshold_db ?? -1
                ),

            startupVolumeInput:
                Number(settings.startup_volume ?? 20),

            maximumVolumeInput:
                Number(settings.maximum_volume ?? 100),

            outputModeInput:
                settings.output_mode ?? "stereo",

            swapChannelsInput:
                Boolean(settings.swap_channels)
        };

        Object.entries(assignments)
            .forEach(([id, value]) => {
                const target = element(id);

                if (!target) {
                    return;
                }

                if (target.type === "checkbox") {
                    target.checked = Boolean(value);
                } else {
                    target.value = String(value);
                }

                target.dispatchEvent(
                    new Event("input")
                );
            });

        renderEqualizer();
        updateDspStatus();
        setChanged(false);
    }

    async function load() {
        const loading = element("audioLoading");
        const content = element("audioContent");

        loading?.classList.remove("hidden");
        content?.classList.add("hidden");

        try {
            const settings =
                await window.OpenAudioApi.getJson(
                    "/api/audio/settings"
                );

            applySettings(settings);

            loading?.classList.add("hidden");
            content?.classList.remove("hidden");
        } catch (error) {
            console.error(
                "Audio-Einstellungen konnten nicht geladen werden:",
                error
            );

            /*
             * Bis das Backend eingebaut ist, wird eine
             * funktionsfähige Standardkonfiguration angezeigt.
             */
            applySettings({
                equalizer_enabled: false,
                equalizer_mode: 10,
                preset: "flat",
                bands: [...presets.flat],
                bass: 0,
                treble: 0,
                balance: 0,
                loudness_enabled: false,
                limiter_enabled: true,
                limiter_threshold_db: -1,
                startup_volume: 20,
                maximum_volume: 100,
                output_mode: "stereo",
                swap_channels: false
            });

            loading?.classList.add("hidden");
            content?.classList.remove("hidden");

            showMessage(
                "Audio-Backend ist noch nicht verfügbar. Standardwerte werden angezeigt.",
                "info"
            );
        }
    }

    async function save() {
        const saveButton =
            element("saveAudioPageButton");

        const settings =
            collectSettings();

        if (
            settings.startup_volume >
            settings.maximum_volume
        ) {
            showMessage(
                "Die Startlautstärke darf nicht über der maximalen Lautstärke liegen.",
                "error"
            );

            return;
        }

        if (saveButton) {
            saveButton.disabled = true;
            saveButton.textContent =
                "Wird gespeichert …";
        }

        try {
            await window.OpenAudioApi.postJson(
                "/api/audio/settings",
                settings
            );

            currentSettings = settings;
            setChanged(false);

            showMessage(
                "Audio-Einstellungen wurden gespeichert."
            );
        } catch (error) {
            showMessage(
                `Speichern fehlgeschlagen: ${error.message}`,
                "error"
            );
        } finally {
            if (saveButton) {
                saveButton.disabled = false;
                saveButton.textContent =
                    "Audio-Einstellungen speichern";
            }
        }
    }

    async function resetSettings() {
        const confirmed = window.confirm(
            "Sollen alle Audio-Einstellungen auf Werkseinstellungen zurückgesetzt werden?"
        );

        if (!confirmed) {
            return;
        }

        try {
            const settings =
                await window.OpenAudioApi.postJson(
                    "/api/audio/reset",
                    {}
                );

            applySettings(settings);

            showMessage(
                "Audio-Einstellungen wurden zurückgesetzt."
            );
        } catch (error) {
            showMessage(
                `Zurücksetzen fehlgeschlagen: ${error.message}`,
                "error"
            );
        }
    }

    function bindEvents() {
        element("equalizerEnabledInput")
            ?.addEventListener(
                "change",
                () => {
                    updateDspStatus();
                    setChanged();
                }
            );

        element("equalizerModeInput")
            ?.addEventListener(
                "change",
                () => {
                    renderEqualizer();
                    setChanged();
                }
            );

        element("equalizerPresetInput")
            ?.addEventListener(
                "change",
                (event) => {
                    applyPreset(event.target.value);
                }
            );

        element("flattenEqualizerButton")
            ?.addEventListener(
                "click",
                () => {
                    equalizerValues =
                        [...presets.flat];

                    const preset =
                        element(
                            "equalizerPresetInput"
                        );

                    if (preset) {
                        preset.value = "flat";
                    }

                    renderEqualizer();
                    setChanged();
                }
            );

        element("saveAudioPageButton")
            ?.addEventListener(
                "click",
                save
            );

        element("resetAudioSettingsButton")
            ?.addEventListener(
                "click",
                resetSettings
            );

        document
            .querySelectorAll(
                "#audioPage input, #audioPage select"
            )
            .forEach((input) => {
                input.addEventListener(
                    "change",
                    () => setChanged()
                );
            });
    }

    function init() {
        if (initialized) {
            return;
        }

        if (!element("audioPage")) {
            console.error(
                "Audio-Seite wurde nicht gefunden."
            );
            return;
        }

        setRangeOutput(
            "bassInput",
            "bassValue",
            formatDb
        );

        setRangeOutput(
            "trebleInput",
            "trebleValue",
            formatDb
        );

        setRangeOutput(
            "balanceInput",
            "balanceValue",
            updateBalanceOutput
        );

        setRangeOutput(
            "limiterThresholdInput",
            "limiterThresholdValue",
            formatDb
        );

        setRangeOutput(
            "startupVolumeInput",
            "startupVolumeValue",
            (value) => `${value} %`
        );

        setRangeOutput(
            "maximumVolumeInput",
            "maximumVolumeValue",
            (value) => `${value} %`
        );

        bindEvents();
        renderEqualizer();

        initialized = true;

        console.log(
            "OpenAudio32 Audio-Modul gestartet"
        );
    }

function destroy() {
    initialized = false;
    changed = false;
    currentSettings = null;
    equalizerValues = [...presets.flat];
}

    return {
        init,
        load,
        save,
        destroy
    };
})();

window.initializeAudioPage =
    async function () {
        window.OpenAudioAudio.init();
        await window.OpenAudioAudio.load();
    };