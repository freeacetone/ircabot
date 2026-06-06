/*
 * IRCaBot Reborn: real time chat reading.
 * The only JavaScript in the whole interface, used only on /~realtime/ pages.
 */
"use strict";

(function () {
    var log = document.getElementById("live-log");
    var status = document.getElementById("live-status");
    if (!log) return;

    var server = log.dataset.server;
    var channel = log.dataset.channel;
    var lastId = "0";
    var failures = 0;
    var POLL_MS = 3000;
    var MAX_LINES = 500;

    function setStatus(text, ok) {
        if (!status) return;
        status.textContent = text;
        status.style.color = ok ? "var(--green)" : "var(--red)";
    }

    function appendMessage(msg) {
        var line = document.createElement("div");
        line.className = "line";

        var time = document.createElement("span");
        time.className = "ln";
        var date = new Date(msg.time * 1000);
        time.textContent =
            ("0" + date.getHours()).slice(-2) + ":" + ("0" + date.getMinutes()).slice(-2);

        var nick = document.createElement("span");
        nick.className = "nick";
        nick.style.setProperty("--h", msg.hue); // theme decides the rest
        nick.textContent = msg.nick;

        var text = document.createElement("span");
        text.className = "msg";
        text.textContent = msg.text; // textContent: no HTML injection possible

        line.appendChild(time);
        line.appendChild(nick);
        line.appendChild(text);
        log.appendChild(line);

        while (log.childElementCount > MAX_LINES) {
            log.removeChild(log.firstElementChild);
        }
    }

    function poll() {
        var request = new XMLHttpRequest();
        request.open("GET", "/ajax/" + server + "/" + channel + "?after=" + lastId, true);
        request.timeout = POLL_MS * 2;
        request.onload = function () {
            failures = 0;
            var data;
            try {
                data = JSON.parse(request.responseText);
            } catch (e) {
                setStatus("bad answer", false);
                return;
            }
            if (!data.ok) {
                setStatus("error", false);
                return;
            }
            setStatus(data.connected ? "receiving" : "bot offline", data.connected);

            var atBottom =
                window.innerHeight + window.scrollY >= document.body.offsetHeight - 60;
            var messages = data.messages || [];
            for (var i = 0; i < messages.length; i++) {
                appendMessage(messages[i]);
            }
            lastId = data.last || lastId;
            if (messages.length > 0 && atBottom) {
                window.scrollTo(0, document.body.scrollHeight);
            }
        };
        request.onerror = request.ontimeout = function () {
            failures++;
            setStatus("connection lost", false);
        };
        request.send();
    }

    setStatus("connecting...", true);
    poll();
    setInterval(poll, POLL_MS);
})();
