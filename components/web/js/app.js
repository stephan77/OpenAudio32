"use strict";

const pageConfiguration = {
    player: {
        url: "/pages/player.html",
        title: "Player",
        eyebrow: "Jetzt läuft",
        initializer: "initializePlayerPage"
    },

    stations: {
        url: "/pages/stations.html",
        title: "Radiosender",
        eyebrow: "Senderverwaltung",
        initializer: "initializeStationsPage"
    },

    audio: {
        url: "/pages/audio.html",
        title: "Audio",
        eyebrow: "Klangregelung",
        initializer: "initializeAudioPage"
    },

    wifi: {
        url: "/pages/wifi.html",
        title: "WLAN",
        eyebrow: "Netzwerk",
        initializer: "initializeWifiPage"
    },

    spotify: {
        url: "/pages/spotify.html",
        title: "Spotify",
        eyebrow: "Streaming-Dienste",
        initializer: "initializeSpotifyPage"
    },

    system: {
        url: "/pages/system.html",
        title: "System",
        eyebrow: "Geräteverwaltung",
        initializer: "initializeSystemPage"
    }
};

let currentPage = null;
let pageLoadController = null;

async function fetchHtml(url, signal = undefined) {
    const response = await fetch(url, {
        cache: "no-store",
        signal
    });

    if (!response.ok) {
        throw new Error(
            `HTTP ${response.status} beim Laden von ${url}`
        );
    }

    return response.text();
}

async function loadModalContent() {
    const modalContainer =
        document.getElementById("modalContainer");

    if (!modalContainer) {
        return;
    }

    try {
        modalContainer.innerHTML =
            await fetchHtml(
                "/pages/station-modal.html"
            );
    } catch (error) {
        console.error(
            "Senderdialog konnte nicht geladen werden:",
            error
        );
    }
}

async function loadPage(pageName) {
    if (
        currentPage === "player" &&
        window.OpenAudioPlayer &&
        typeof window.OpenAudioPlayer.destroy === "function"
    ) {
        window.OpenAudioPlayer.destroy();
    }

    if (
        currentPage === "audio" &&
        window.OpenAudioAudio &&
        typeof window.OpenAudioAudio.destroy === "function"
    ) {
        window.OpenAudioAudio.destroy();
    }

    if (
        currentPage === "stations" &&
        window.OpenAudioStations &&
        typeof window.OpenAudioStations.destroy === "function"
    ) {
        window.OpenAudioStations.destroy();
    }

    const configuration =
        pageConfiguration[pageName];

    const pageContent =
        document.getElementById("pageContent");

    if (!configuration || !pageContent) {
        return;
    }

    if (pageLoadController) {
        pageLoadController.abort();
    }

    const controller =
        new AbortController();

    pageLoadController =
        controller;

    pageContent.innerHTML = `
        <div class="page-loading">
            Seite wird geladen …
        </div>
    `;

    try {
        const html =
            await fetchHtml(
                configuration.url,
                controller.signal
            );

        /*
         * Falls inzwischen bereits eine andere Seite angefordert wurde,
         * darf dieses ältere Ergebnis nicht mehr eingesetzt werden.
         */
        if (pageLoadController !== controller) {
            return;
        }

pageContent.innerHTML =
    html;

const loadedPage =
    pageContent.querySelector(
        "[data-page-section]"
    );

if (loadedPage) {
    loadedPage.classList.add(
        "active"
    );
}

currentPage =
    pageName;

        updateNavigation(
            pageName
        );

        updatePageHeader(
            configuration
        );

        const initializer =
            window[
                configuration.initializer
            ];

        if (typeof initializer === "function") {
            await initializer();
        } else {
            console.debug(
                `Kein Initializer für ${pageName} registriert`
            );
        }
    } catch (error) {
        if (error.name === "AbortError") {
            return;
        }

        console.error(
            `Seite ${pageName} konnte nicht geladen werden:`,
            error
        );

        pageContent.innerHTML = `
            <div class="empty-state">
                <div class="empty-icon">
                    !
                </div>

                <h3>
                    Seite konnte nicht geladen werden
                </h3>

                <p>
                    ${escapeHtml(error.message)}
                </p>
            </div>
        `;
    } finally {
        if (pageLoadController === controller) {
            pageLoadController = null;
        }
    }
}

function updateNavigation(pageName) {
    document
        .querySelectorAll(".nav-item")
        .forEach((button) => {
            button.classList.toggle(
                "active",
                button.dataset.page === pageName
            );
        });
}

function updatePageHeader(configuration) {
    const pageTitle =
        document.getElementById("pageTitle");

    const pageEyebrow =
        document.getElementById("pageEyebrow");

    if (pageTitle) {
        pageTitle.textContent =
            configuration.title;
    }

    if (pageEyebrow) {
        pageEyebrow.textContent =
            configuration.eyebrow;
    }
}

function escapeHtml(value) {
    return String(value)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#039;");
}

function closeMobileNavigation() {
    const sidebar =
        document.querySelector(".sidebar");

    if (sidebar) {
        sidebar.classList.remove(
            "open"
        );
    }
}

function registerNavigation() {
    document
        .querySelectorAll(".nav-item")
        .forEach((button) => {
            button.addEventListener(
                "click",
                async () => {
                    const pageName =
                        button.dataset.page;

                    if (!pageName ||
                        !pageConfiguration[pageName] ||
                        pageName === currentPage) {

                        closeMobileNavigation();
                        return;
                    }

                    closeMobileNavigation();

                    await loadPage(
                        pageName
                    );
                }
            );
        });
}

function registerMobileMenu() {
    const menuButton =
        document.getElementById("menuButton");

    const sidebar =
        document.querySelector(".sidebar");

    if (!menuButton || !sidebar) {
        return;
    }

    menuButton.addEventListener(
        "click",
        () => {
            sidebar.classList.toggle(
                "open"
            );
        }
    );
}

function updateDeviceIp() {
    const deviceIp =
        document.getElementById("deviceIp");

    if (!deviceIp) {
        return;
    }

    deviceIp.textContent =
        window.location.hostname || "–";
}

async function initializeApplication() {
    registerNavigation();
    registerMobileMenu();
    updateDeviceIp();

    await loadModalContent();
    await loadPage("player");
}

document.addEventListener(
    "DOMContentLoaded",
    initializeApplication
);