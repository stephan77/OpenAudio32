"use strict";

window.OpenAudioWifi = (() => {
    const $ = (id) => document.getElementById(id);
    let globalEventsBound = false;
    let networks = [];
    let savedNetworks = [];
    let selectedNetwork = null;

    function showMessage(text, type = "success") {
        const box = $("wifiMessage");
        if (!box) return;
        box.textContent = text;
        box.className = `inline-message ${type}`;
        window.setTimeout(() => box.classList.add("hidden"), 5000);
    }

    function signalLabel(rssi) {
        if (rssi >= -50) return "Ausgezeichnet";
        if (rssi >= -60) return "Sehr gut";
        if (rssi >= -70) return "Gut";
        if (rssi >= -80) return "Schwach";
        return "Sehr schwach";
    }

    function securityLabel(value) {
        return value || "Offen";
    }

    function setText(id, value) {
        const node = $(id);
        if (node) node.textContent = value ?? "–";
    }

    function applyStatus(status) {
        const connected = Boolean(status.connected);
        const badge = $("wifiConnectionBadge");
        if (badge) {
            badge.textContent = connected ? "Verbunden" : "Nicht verbunden";
            badge.className = `wifi-status-badge ${connected ? "online" : "offline"}`;
        }
        setText("wifiCurrentSsid", connected ? status.ssid : "Nicht verbunden");
        setText("wifiCurrentState", connected ? "Online" : "Offline");
        setText("wifiCurrentSecurity", securityLabel(status.security));
        setText("wifiCurrentRssi", connected ? `${status.rssi} dBm · ${signalLabel(status.rssi)}` : "– dBm");
        setText("wifiCurrentChannel", status.channel ?? "–");
        setText("wifiCurrentIp", status.ip);
        setText("wifiCurrentGateway", status.gateway);
        setText("wifiCurrentDns", status.dns);
        setText("wifiCurrentMac", status.mac);
        setText("wifiCurrentHostname", status.hostname || "OpenAudio32");
        setText("wifiInternetState", status.internet ? "Erreichbar" : "Nicht geprüft");
        setText("wifiApIp", status.ap?.ip || "192.168.4.1");
        setText("wifiApClients", String(status.ap?.clients ?? 0));
        const apEnabled = $("wifiApEnabledInput");
        if (apEnabled) apEnabled.checked = Boolean(status.ap?.enabled);
    }

    function renderNetworks() {
        const list = $("wifiNetworksList");
        const empty = $("wifiNetworksEmpty");
        if (!list || !empty) return;
        const query = ($("wifiNetworkSearchInput")?.value || "").toLowerCase();
        const showHidden = Boolean($("wifiShowHiddenInput")?.checked);
        const filtered = networks.filter((network) => {
            if (!showHidden && network.hidden) return false;
            return String(network.ssid || "").toLowerCase().includes(query);
        });
        list.innerHTML = "";
        empty.classList.toggle("hidden", filtered.length !== 0);
        filtered.forEach((network) => {
            const row = document.createElement("div");
            row.className = "wifi-network-row";
            row.innerHTML = `
                <div class="wifi-network-icon">⌁</div>
                <div class="wifi-network-main">
                    <strong>${network.ssid || "Verstecktes Netzwerk"}</strong>
                    <span>${network.rssi} dBm · ${signalLabel(network.rssi)} · Kanal ${network.channel} · ${securityLabel(network.security)}</span>
                </div>
                <div class="wifi-network-actions">
                    <button class="wifi-row-button primary" type="button">Verbinden</button>
                </div>`;
            row.querySelector("button")?.addEventListener("click", () => openConnectModal(network));
            list.appendChild(row);
        });
    }

    function renderSavedNetworks() {
        const list = $("wifiSavedList");
        const empty = $("wifiSavedEmpty");
        if (!list || !empty) return;
        list.innerHTML = "";
        empty.classList.toggle("hidden", savedNetworks.length !== 0);
        setText("wifiSavedCount", `${savedNetworks.length} Netzwerk${savedNetworks.length === 1 ? "" : "e"}`);
        savedNetworks.forEach((network, index) => {
            const row = document.createElement("div");
            row.className = "wifi-saved-row";
            row.innerHTML = `
                <div class="wifi-priority">${index + 1}</div>
                <div class="wifi-network-main">
                    <strong>${network.ssid}</strong>
                    <span>${network.auto_connect ? "Automatisch verbinden" : "Nur manuell"}${network.last_connected ? ` · Zuletzt ${network.last_connected}` : ""}</span>
                </div>
                <div class="wifi-network-actions">
                    <button class="wifi-row-button primary" data-action="connect" type="button">Verbinden</button>
                    <button class="wifi-row-button" data-action="up" type="button">↑</button>
                    <button class="wifi-row-button" data-action="down" type="button">↓</button>
                    <button class="wifi-row-button danger" data-action="forget" type="button">Vergessen</button>
                </div>`;
            row.querySelectorAll("button").forEach((button) => {
                button.addEventListener("click", () => handleSavedAction(button.dataset.action, network, index));
            });
            list.appendChild(row);
        });
    }

    async function load() {
        $("wifiLoading")?.classList.remove("hidden");
        $("wifiContent")?.classList.add("hidden");
        try {
            const [status, saved, config] = await Promise.all([
                window.OpenAudioApi.getJson("/api/wifi/status"),
                window.OpenAudioApi.getJson("/api/wifi/saved"),
                window.OpenAudioApi.getJson("/api/wifi/config")
            ]);
            applyStatus(status);
            savedNetworks = Array.isArray(saved.networks) ? saved.networks : [];
            renderSavedNetworks();
            if ($("wifiApSsidInput")) $("wifiApSsidInput").value = config.ap_ssid || "OpenAudio32-Setup";
            if ($("wifiApModeInput")) $("wifiApModeInput").value = config.ap_mode || "fallback";
            if ($("wifiHostnameInput")) $("wifiHostnameInput").value = config.hostname || "OpenAudio32";
            if ($("wifiDhcpInput")) $("wifiDhcpInput").checked = config.dhcp !== false;
        } catch (error) {
            console.error("WLAN-Daten konnten nicht geladen werden:", error);
            showMessage(`WLAN-Daten konnten nicht geladen werden: ${error.message}`, "error");
        } finally {
            $("wifiLoading")?.classList.add("hidden");
            $("wifiContent")?.classList.remove("hidden");
        }
    }

    async function scan() {
        $("wifiNetworksLoading")?.classList.remove("hidden");
        $("wifiNetworksList")?.classList.add("hidden");
        try {
            const result = await window.OpenAudioApi.postJson("/api/wifi/scan", {});
            networks = Array.isArray(result.networks) ? result.networks : [];
            renderNetworks();
            setText("wifiScanTimestamp", `Aktualisiert ${new Date().toLocaleTimeString("de-DE", {hour:"2-digit", minute:"2-digit"})}`);
        } catch (error) {
            showMessage(`WLAN-Scan fehlgeschlagen: ${error.message}`, "error");
        } finally {
            $("wifiNetworksLoading")?.classList.add("hidden");
            $("wifiNetworksList")?.classList.remove("hidden");
        }
    }

    function openConnectModal(network) {
        selectedNetwork = network;
        setText("wifiConnectModalTitle", network.ssid || "Verstecktes Netzwerk");
        if ($("wifiConnectSsidInput")) $("wifiConnectSsidInput").value = network.ssid || "";
        if ($("wifiConnectPasswordInput")) $("wifiConnectPasswordInput").value = "";
        $("wifiConnectModal")?.classList.remove("hidden");
        window.setTimeout(() => $("wifiConnectPasswordInput")?.focus(), 30);
    }

    function closeConnectModal() {
        $("wifiConnectModal")?.classList.add("hidden");
        selectedNetwork = null;
    }

    async function submitConnect(event) {
        event.preventDefault();
        const button = $("wifiConnectSubmitButton");
        if (button) button.disabled = true;
        try {
            await window.OpenAudioApi.postJson("/api/wifi/connect", {
                ssid: $("wifiConnectSsidInput")?.value || selectedNetwork?.ssid || "",
                password: $("wifiConnectPasswordInput")?.value || "",
                save: Boolean($("wifiConnectSaveInput")?.checked),
                auto_connect: Boolean($("wifiConnectAutoInput")?.checked)
            });
            closeConnectModal();
            showMessage("WLAN-Verbindung wird hergestellt.", "info");
            window.setTimeout(load, 1800);
        } catch (error) {
            const box = $("wifiConnectError");
            if (box) { box.textContent = error.message; box.classList.remove("hidden"); }
        } finally {
            if (button) button.disabled = false;
        }
    }

    async function handleSavedAction(action, network, index) {
        try {
            if (action === "connect") {
                await window.OpenAudioApi.postJson("/api/wifi/connect-saved", { id: network.id });
            } else if (action === "forget") {
                if (!window.confirm(`Netzwerk „${network.ssid}“ wirklich vergessen?`)) return;
                await window.OpenAudioApi.postJson("/api/wifi/forget", { id: network.id });
            } else if (action === "up" || action === "down") {
                const target = action === "up" ? index - 1 : index + 1;
                if (target < 0 || target >= savedNetworks.length) return;
                const reordered = [...savedNetworks];
                [reordered[index], reordered[target]] = [reordered[target], reordered[index]];
                await window.OpenAudioApi.postJson("/api/wifi/reorder", { ids: reordered.map((entry) => entry.id) });
            }
            await load();
        } catch (error) {
            showMessage(`Aktion fehlgeschlagen: ${error.message}`, "error");
        }
    }

    async function saveAp() {
        try {
            await window.OpenAudioApi.postJson("/api/wifi/ap", {
                enabled: Boolean($("wifiApEnabledInput")?.checked),
                ssid: $("wifiApSsidInput")?.value.trim(),
                password: $("wifiApPasswordInput")?.value,
                mode: $("wifiApModeInput")?.value
            });
            showMessage("Setup-WLAN wurde gespeichert.");
            await load();
        } catch (error) {
            showMessage(`Setup-WLAN konnte nicht gespeichert werden: ${error.message}`, "error");
        }
    }

    async function disconnect() {
        try { await window.OpenAudioApi.postJson("/api/wifi/disconnect", {}); await load(); }
        catch (error) { showMessage(error.message, "error"); }
    }

    async function reconnect() {
        try { await window.OpenAudioApi.postJson("/api/wifi/reconnect", {}); showMessage("Verbindung wird neu aufgebaut.", "info"); window.setTimeout(load, 1500); }
        catch (error) { showMessage(error.message, "error"); }
    }

    function bindEvents() {
        $("wifiScanButton")?.addEventListener("click", scan);
        $("wifiNetworkSearchInput")?.addEventListener("input", renderNetworks);
        $("wifiShowHiddenInput")?.addEventListener("change", renderNetworks);
        $("wifiReconnectButton")?.addEventListener("click", reconnect);
        $("wifiDisconnectButton")?.addEventListener("click", disconnect);
        $("wifiSaveApButton")?.addEventListener("click", saveAp);
        $("wifiConnectForm")?.addEventListener("submit", submitConnect);
        $("wifiConnectModalCloseButton")?.addEventListener("click", closeConnectModal);
        $("wifiConnectCancelButton")?.addEventListener("click", closeConnectModal);
        $("wifiDhcpInput")?.addEventListener("change", () => $("wifiStaticIpFields")?.classList.toggle("hidden", Boolean($("wifiDhcpInput")?.checked)));
        document.querySelectorAll("[data-password-target]").forEach((button) => {
            button.addEventListener("click", () => {
                const input = $(button.dataset.passwordTarget);
                if (!input) return;
                input.type = input.type === "password" ? "text" : "password";
                button.textContent = input.type === "password" ? "Anzeigen" : "Verbergen";
            });
        });
        if (!globalEventsBound) {
            document.addEventListener("keydown", (event) => {
                if (event.key === "Escape") closeConnectModal();
            });
            globalEventsBound = true;
        }
    }

    function init() {
        if (!$("wifiPage")) return false;
        bindEvents();
        return true;
    }

    return { init, load, scan };
})();

window.initializeWifiPage = async function () {
    if (!window.OpenAudioWifi.init()) return;
    await window.OpenAudioWifi.load();
};
