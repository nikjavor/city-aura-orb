// index_html.h

#pragma once
#include <Arduino.h>

const char html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <link rel="icon" href="data:," />
    <style>
      body {
        font-family: Inter, sans-serif;
        max-width: 360px;
        margin: 20px auto;
      }

      input {
        width: 100%;
        padding: 6px;
        margin-bottom: 10px;
        box-sizing: border-box;
      }

      li {
        cursor: pointer;
        margin-bottom: 4px;
      }

      li:hover {
        text-decoration: underline;
      }

      .status {
        font-size: 12px;
        margin-top: 10px;
      }

      .hidden {
        display: none;
      }
    </style>
  </head>

  <body>
    <h2>WiFi Setup</h2>

    <button id="searchBtn">Search networks</button>
    <ul id="networkList"></ul>

    <form id="wifiForm">
      <p>SSID</p>
      <input id="ssid" type="text" readonly placeholder="Select a network" required />

      <!-- 🔒 Password container -->
      <div id="passwordContainer" class="hidden">
        <p>Password</p>
        <input id="password" type="password" />
      </div>

      <button type="submit">Connect</button>
    </form>

    <p class="status" id="status"></p>

    <script>
      const searchBtn = document.getElementById("searchBtn");
      const networkList = document.getElementById("networkList");
      const ssidInput = document.getElementById("ssid");
      const passwordContainer = document.getElementById("passwordContainer");
      const form = document.getElementById("wifiForm");
      const statusText = document.getElementById("status");

      let selectedNetwork = null;

      // 🔍 Scan networks
      searchBtn.addEventListener("click", async () => {
        networkList.innerHTML = "<li>Loading...</li>";
        passwordContainer.classList.add("hidden");
        ssidInput.value = "";
        selectedNetwork = null;

        try {
          const res = await fetch("/networks", { method: "POST" });
          const networks = await res.json();

          networkList.innerHTML = "";

          networks.forEach((net) => {
            const li = document.createElement("li");
            li.textContent = net.ssid + (net.protected ? " 🔒" : "");

            li.onclick = () => {
              selectedNetwork = net;
              ssidInput.value = net.ssid;

              // 🔐 toggle password field
              if (net.protected) {
                passwordContainer.classList.remove("hidden");
              } else {
                passwordContainer.classList.add("hidden");
              }
            };

            networkList.appendChild(li);
          });

          if (networks.length === 0) {
            networkList.innerHTML = "<li>No networks found</li>";
          }
        } catch (err) {
          networkList.innerHTML = "<li>Error loading networks</li>";
        }
      });

      // 🔌 Connect
      form.addEventListener("submit", async (e) => {
        e.preventDefault();

        if (!selectedNetwork) {
          statusText.textContent = "Please select a network";
          return;
        }

        const password = document.getElementById("password").value;

        statusText.textContent = "Connecting...";

        try {
          const res = await fetch("/connect", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
            },
            body: JSON.stringify({
              ssid: selectedNetwork.ssid,
              pass: selectedNetwork.protected ? password : "",
            }),
          });

          const result = await res.json();

          statusText.textContent = result.success
            ? "Connected!"
            : "Failed to connect";
        } catch {
          statusText.textContent = "Error connecting";
        }
      });
    </script>
  </body>
</html>
)rawliteral";