"use strict";

document.addEventListener(
    "DOMContentLoaded",
    () => {
        const navigationItems =
            document.querySelectorAll(".nav-item");

        const pageSections =
            document.querySelectorAll(
                "[data-page-section]"
            );

        const pageTitle =
            document.getElementById("pageTitle");

        const pageEyebrow =
            document.getElementById("pageEyebrow");

        const sidebar =
            document.querySelector(".sidebar");

        const menuButton =
            document.getElementById("menuButton");

        const pageConfiguration = {
            player: {
                title: "Player",
                eyebrow: "Jetzt läuft"
            },

            stations: {
                title: "Radiosender",
                eyebrow: "Senderverwaltung"
            },

            audio: {
                title: "Audio & Equalizer",
                eyebrow: "Klangregelung"
            },

            wifi: {
                title: "WLAN",
                eyebrow: "Netzwerk"
            },

            spotify: {
                title: "Spotify Connect",
                eyebrow: "Streaming-Dienste"
            },

            system: {
                title: "System",
                eyebrow: "Geräteverwaltung"
            }
        };

        function openPage(pageName) {
            const configuration =
                pageConfiguration[pageName] ??
                pageConfiguration.player;

            navigationItems.forEach((item) => {
                item.classList.toggle(
                    "active",
                    item.dataset.page === pageName
                );
            });

            pageSections.forEach((section) => {
                section.classList.toggle(
                    "active",
                    section.dataset.pageSection ===
                        pageName
                );
            });

            if (pageTitle) {
                pageTitle.textContent =
                    configuration.title;
            }

            if (pageEyebrow) {
                pageEyebrow.textContent =
                    configuration.eyebrow;
            }

if (
    pageName === "stations" &&
    window.OpenAudioStations &&
    typeof window.OpenAudioStations.load ===
        "function"
) {
    window.OpenAudioStations.load();
}
if (
    pageName === "system" &&
    window.OpenAudioSystem
) {
    window.OpenAudioSystem.loadSettings();
    window.OpenAudioSystem.loadStatus();
}
            sidebar?.classList.remove("open");
        }

        navigationItems.forEach((item) => {
            item.addEventListener(
                "click",
                () => {
                    openPage(item.dataset.page);
                }
            );
        });

        menuButton?.addEventListener(
            "click",
            () => {
                sidebar?.classList.toggle("open");
            }
        );

        if (window.OpenAudioPlayer) {
            window.OpenAudioPlayer.init();
        } else {
            console.error(
                "OpenAudioPlayer wurde nicht geladen"
            );
        }

if (
    window.OpenAudioStations &&
    typeof window.OpenAudioStations.init ===
        "function"
) {
    window.OpenAudioStations.init();
} else {
    console.error(
        "OpenAudioStations wurde nicht geladen"
    );
}
if (

    window.OpenAudioSystem &&

    typeof window.OpenAudioSystem.init ===

        "function"

) {

    window.OpenAudioSystem.init();

}
        openPage("player");

        console.log(
            "OpenAudio32 Webanwendung gestartet"
        );
    }
);