"use strict";

window.OpenAudioStations = (() => {
    const $ = (id) => document.getElementById(id);

    let stations = [];
    let editingStationId = null;
    let initialized = false;
    let messageTimer = null;

    let stationsGrid;
    let stationsLoading;
    let stationsEmpty;
    let stationsMessage;

    let addStationButton;
    let emptyAddStationButton;

    let stationModal;
    let stationModalTitle;
    let closeStationModalButton;
    let cancelStationButton;

    let stationForm;
    let stationNameInput;
    let stationUrlInput;
    let stationFavoriteInput;
    let stationFormError;
    let saveStationButton;

    function escapeHtml(value) {
        return String(value ?? "")
            .replaceAll("&", "&amp;")
            .replaceAll("<", "&lt;")
            .replaceAll(">", "&gt;")
            .replaceAll('"', "&quot;")
            .replaceAll("'", "&#039;");
    }

    function createInitials(name) {
        const words = String(name ?? "")
            .trim()
            .split(/\s+/)
            .filter(Boolean);

        if (words.length === 0) {
            return "OA";
        }

        return words
            .slice(0, 2)
            .map((word) => word.charAt(0))
            .join("")
            .toUpperCase();
    }

    function showMessage(message, type = "success") {
        if (!stationsMessage) {
            return;
        }

        window.clearTimeout(messageTimer);

        stationsMessage.textContent = message;
        stationsMessage.className =
            `inline-message ${type}`;

        messageTimer = window.setTimeout(() => {
            stationsMessage.classList.add("hidden");
        }, 5000);
    }

    function showFormError(message) {
        if (!stationFormError) {
            return;
        }

        stationFormError.textContent = message;
        stationFormError.classList.remove("hidden");
    }

    function clearFormError() {
        if (!stationFormError) {
            return;
        }

        stationFormError.textContent = "";
        stationFormError.classList.add("hidden");
    }

    function setModalBusy(busy) {
        if (!saveStationButton) {
            return;
        }

        saveStationButton.disabled = busy;

        saveStationButton.textContent = busy
            ? "Wird gespeichert …"
            : editingStationId === null
                ? "Sender speichern"
                : "Änderungen speichern";
    }

    function openAddModal() {
        editingStationId = null;

        stationForm?.reset();
        clearFormError();

        if (stationModalTitle) {
            stationModalTitle.textContent =
                "Neuen Sender hinzufügen";
        }

        setModalBusy(false);
        stationModal?.classList.remove("hidden");

        window.setTimeout(() => {
            stationNameInput?.focus();
        }, 50);
    }

    function openEditModal(stationId) {
        const station = stations.find(
            (entry) => Number(entry.id) === Number(stationId)
        );

        if (!station) {
            showMessage(
                "Der Sender wurde nicht gefunden.",
                "error"
            );
            return;
        }

        editingStationId = Number(station.id);
        clearFormError();

        if (stationModalTitle) {
            stationModalTitle.textContent =
                "Sender bearbeiten";
        }

        if (stationNameInput) {
            stationNameInput.value =
                station.name ?? "";
        }

        if (stationUrlInput) {
            stationUrlInput.value =
                station.url ?? "";
        }

        if (stationFavoriteInput) {
            stationFavoriteInput.checked =
                Boolean(station.favorite);
        }

        setModalBusy(false);
        stationModal?.classList.remove("hidden");

        window.setTimeout(() => {
            stationNameInput?.focus();
            stationNameInput?.select();
        }, 50);
    }

    function closeModal() {
        stationModal?.classList.add("hidden");

        stationForm?.reset();
        clearFormError();

        editingStationId = null;
    }

    function setLoading(loading) {
        stationsLoading?.classList.toggle(
            "hidden",
            !loading
        );

        if (loading) {
            stationsEmpty?.classList.add("hidden");
            stationsGrid?.classList.add("hidden");
        }
    }

    function updateEmptyState() {
        const isEmpty = stations.length === 0;

        stationsEmpty?.classList.toggle(
            "hidden",
            !isEmpty
        );

        stationsGrid?.classList.toggle(
            "hidden",
            isEmpty
        );
    }

    function createStationCard(station) {
        const card = document.createElement("article");

        card.className = station.current
            ? "station-card current"
            : "station-card";

        card.dataset.stationId =
            String(station.id);

        const currentBadge = station.current
            ? `
                <span class="station-badge current">
                    <span class="badge-dot"></span>
                    Aktuell
                </span>
            `
            : "";

        const favoriteBadge = station.favorite
            ? `
                <span class="station-badge favorite">
                    ★ Favorit
                </span>
            `
            : "";

        card.innerHTML = `
            <div class="station-card-top">
                <div class="station-card-logo">
                    ${escapeHtml(
                        createInitials(station.name)
                    )}
                </div>

                <div class="station-card-heading">
                    <h4>
                        ${escapeHtml(station.name)}
                    </h4>

                    <div class="station-card-badges">
                        ${currentBadge}
                        ${favoriteBadge}
                    </div>
                </div>

                <button
                    class="station-favorite-button
                        ${station.favorite ? "active" : ""}"
                    type="button"
                    data-action="favorite"
                    aria-label="Favorit umschalten"
                    title="Favorit umschalten"
                >
                    ${station.favorite ? "★" : "☆"}
                </button>
            </div>

            <div class="station-stream-information">
                <span class="station-stream-label">
                    Stream-Adresse
                </span>

                <span
                    class="station-url"
                    title="${escapeHtml(station.url)}"
                >
                    ${escapeHtml(station.url)}
                </span>
            </div>

            <div class="station-card-actions">
                <button
                    class="station-action-button play
                        ${station.current ? "current" : ""}"
                    type="button"
                    data-action="play"
                    ${station.current ? "disabled" : ""}
                >
                    <span>
                        ${station.current ? "●" : "▶"}
                    </span>

                    ${station.current
                        ? "Wird abgespielt"
                        : "Abspielen"
                    }
                </button>

                <button
                    class="station-action-button"
                    type="button"
                    data-action="edit"
                >
                    <span>✎</span>
                    Bearbeiten
                </button>

                <button
                    class="station-action-button danger"
                    type="button"
                    data-action="delete"
                >
                    <span>🗑</span>
                    Löschen
                </button>
            </div>
        `;

        card
            .querySelectorAll("[data-action]")
            .forEach((button) => {
                button.addEventListener("click", () => {
                    const action =
                        button.dataset.action;

                    handleCardAction(
                        action,
                        Number(station.id)
                    );
                });
            });

        return card;
    }

    function renderStations() {
        if (!stationsGrid) {
            return;
        }

        stationsGrid.innerHTML = "";

        updateEmptyState();

        if (stations.length === 0) {
            return;
        }

        stations.forEach((station) => {
            stationsGrid.appendChild(
                createStationCard(station)
            );
        });
    }

    async function load() {
        if (!window.OpenAudioApi) {
            showMessage(
                "Die Web-API wurde nicht geladen.",
                "error"
            );
            return;
        }

        setLoading(true);

        try {
            const result =
                await window.OpenAudioApi.getJson(
                    "/api/stations"
                );

            stations = Array.isArray(result.stations)
                ? result.stations
                : [];

            renderStations();

            console.log(
                `${stations.length} Sender geladen`
            );
        } catch (error) {
            stations = [];
            renderStations();

            console.error(
                "Sender konnten nicht geladen werden:",
                error
            );

            showMessage(
                `Sender konnten nicht geladen werden: ${error.message}`,
                "error"
            );
        } finally {
            setLoading(false);
            updateEmptyState();
        }
    }

    async function saveStation(event) {
        event.preventDefault();

        clearFormError();

        const name =
            stationNameInput?.value.trim() ?? "";

        const url =
            stationUrlInput?.value.trim() ?? "";

        const favorite =
            Boolean(stationFavoriteInput?.checked);

        if (!name) {
            showFormError(
                "Bitte einen Sendernamen eingeben."
            );
            stationNameInput?.focus();
            return;
        }

        if (
            !url.startsWith("http://") &&
            !url.startsWith("https://")
        ) {
            showFormError(
                "Die Stream-Adresse muss mit http:// oder https:// beginnen."
            );
            stationUrlInput?.focus();
            return;
        }

        setModalBusy(true);

        try {
            if (editingStationId === null) {
                await window.OpenAudioApi.postJson(
                    "/api/stations",
                    {
                        name,
                        url,
                        favorite
                    }
                );

                closeModal();

                showMessage(
                    `Sender „${name}“ wurde gespeichert.`
                );
            } else {
                await window.OpenAudioApi.postJson(
                    "/api/stations/update",
                    {
                        id: editingStationId,
                        name,
                        url,
                        favorite
                    }
                );

                closeModal();

                showMessage(
                    `Sender „${name}“ wurde aktualisiert.`
                );
            }

            await load();
        } catch (error) {
            console.error(
                "Sender konnte nicht gespeichert werden:",
                error
            );

            showFormError(
                `Speichern fehlgeschlagen: ${error.message}`
            );
        } finally {
            setModalBusy(false);
        }
    }

    async function playStation(stationId) {
        const station = stations.find(
            (entry) => Number(entry.id) === Number(stationId)
        );

        if (!station) {
            return;
        }

        showMessage(
            `Verbinde mit „${station.name}“ …`,
            "info"
        );

        try {
            await window.OpenAudioApi.postJson(
                "/api/stations/select",
                {
                    id: stationId
                }
            );

            showMessage(
                `„${station.name}“ wird jetzt abgespielt.`
            );

            await load();

            if (
                window.OpenAudioPlayer &&
                typeof window.OpenAudioPlayer.loadStatus ===
                    "function"
            ) {
                await window.OpenAudioPlayer.loadStatus();
            }
        } catch (error) {
            console.error(
                "Senderwechsel fehlgeschlagen:",
                error
            );

            showMessage(
                `Senderwechsel fehlgeschlagen: ${error.message}`,
                "error"
            );
        }
    }

    async function deleteStation(stationId) {
        const station = stations.find(
            (entry) => Number(entry.id) === Number(stationId)
        );

        if (!station) {
            return;
        }

        const confirmed = window.confirm(
            `Soll der Sender „${station.name}“ wirklich gelöscht werden?`
        );

        if (!confirmed) {
            return;
        }

        try {
            await window.OpenAudioApi.postJson(
                "/api/stations/delete",
                {
                    id: stationId
                }
            );

            showMessage(
                `Sender „${station.name}“ wurde gelöscht.`
            );

            await load();
        } catch (error) {
            console.error(
                "Sender konnte nicht gelöscht werden:",
                error
            );

            showMessage(
                `Löschen fehlgeschlagen: ${error.message}`,
                "error"
            );
        }
    }

    async function toggleFavorite(stationId) {
        const station = stations.find(
            (entry) => Number(entry.id) === Number(stationId)
        );

        if (!station) {
            return;
        }

        try {
            await window.OpenAudioApi.postJson(
                "/api/stations/update",
                {
                    id: station.id,
                    name: station.name,
                    url: station.url,
                    favorite: !station.favorite
                }
            );

            await load();
        } catch (error) {
            showMessage(
                `Favorit konnte nicht geändert werden: ${error.message}`,
                "error"
            );
        }
    }

    function handleCardAction(action, stationId) {
        switch (action) {
        case "play":
            playStation(stationId);
            break;

        case "edit":
            openEditModal(stationId);
            break;

        case "delete":
            deleteStation(stationId);
            break;

        case "favorite":
            toggleFavorite(stationId);
            break;

        default:
            console.warn(
                "Unbekannte Senderaktion:",
                action
            );
            break;
        }
    }

    function bindEvents() {
        addStationButton?.addEventListener(
            "click",
            openAddModal
        );

        emptyAddStationButton?.addEventListener(
            "click",
            openAddModal
        );

        closeStationModalButton?.addEventListener(
            "click",
            closeModal
        );

        cancelStationButton?.addEventListener(
            "click",
            closeModal
        );

        stationForm?.addEventListener(
            "submit",
            saveStation
        );

        stationModal?.addEventListener(
            "click",
            (event) => {
                if (event.target === stationModal) {
                    closeModal();
                }
            }
        );

        document.addEventListener(
            "keydown",
            (event) => {
                if (
                    event.key === "Escape" &&
                    stationModal &&
                    !stationModal.classList.contains("hidden")
                ) {
                    closeModal();
                }
            }
        );
    }

    function init() {
        if (initialized) {
            return;
        }

        stationsGrid = $("stationsGrid");
        stationsLoading = $("stationsLoading");
        stationsEmpty = $("stationsEmpty");
        stationsMessage = $("stationsMessage");

        addStationButton = $("addStationButton");
        emptyAddStationButton =
            $("emptyAddStationButton");

        stationModal = $("stationModal");
        stationModalTitle =
            $("stationModalTitle");

        closeStationModalButton =
            $("closeStationModalButton");

        cancelStationButton =
            $("cancelStationButton");

        stationForm = $("stationForm");
        stationNameInput =
            $("stationNameInput");

        stationUrlInput =
            $("stationUrlInput");

        stationFavoriteInput =
            $("stationFavoriteInput");

        stationFormError =
            $("stationFormError");

        saveStationButton =
            $("saveStationButton");

        if (!stationsGrid) {
            console.error(
                "Senderseite wurde im HTML nicht gefunden."
            );
            return;
        }

        bindEvents();

        initialized = true;

        console.log(
            "OpenAudio32 Stations-Modul gestartet"
        );
    }

    return {
        init,
        load
    };
})();