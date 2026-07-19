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

    // "back" returns to the page the reader came from; the default href
    // (channel archive) stays when there is no same-origin referrer
    var back = document.getElementById("live-back");
    if (back && document.referrer) {
        try {
            var ref = new URL(document.referrer);
            if (ref.origin === window.location.origin
                    && ref.pathname !== window.location.pathname) {
                back.href = document.referrer;
            }
        } catch (e) { /* keep the fallback */ }
    }
    var lastId = "0";
    var networkOk = true;
    var polling = false; // a request is in flight: never overlap, or lastId
                         // would be reused and the same messages fetched twice
    var POLL_MS = 3000;
    var MAX_LINES = 500;

    // Dots advance only when a real request is sent to the server;
    // green while the network works, red otherwise (as in v1)
    function advanceDots() {
        if (!status) return;
        var dots = status.textContent;
        status.textContent = dots.length >= 3 ? "." : dots + ".";
    }

    function paintDots() {
        if (!status) return;
        status.className = networkOk ? "live-dots" : "live-dots bad";
    }

    function appendMessage(msg) {
        var line = document.createElement("div");
        line.className = "line";

        // The server keeps no receive time (privacy by design), so the moment
        // the line reaches the browser is what we show - the reader's own clock.
        var time = document.createElement("span");
        time.className = "ln";
        var date = new Date();
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
        if (polling) return; // previous request still in flight: skip this tick
        polling = true;
        var request = new XMLHttpRequest();
        request.open("GET", "/~api/" + server + "/" + channel + "?after=" + lastId, true);
        request.timeout = POLL_MS * 2;
        request.onload = function () {
            polling = false;
            var data;
            try {
                data = JSON.parse(request.responseText);
            } catch (e) {
                networkOk = false;
                paintDots();
                return;
            }
            if (!data.ok) {
                networkOk = false;
                paintDots();
                return;
            }
            // Network is fine; red dots also when the bot lost its IRC server
            networkOk = data.connected;
            paintDots();

            // Desktop: the log block scrolls internally. Small screens:
            // the whole content column is the scroller (header slides away)
            var scroller = getComputedStyle(log).overflowY === "auto"
                ? log
                : (document.querySelector(".content.chat") || document.scrollingElement);
            var atBottom =
                scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight < 60;
            var messages = data.messages || [];
            for (var i = 0; i < messages.length; i++) {
                appendMessage(messages[i]);
            }
            lastId = data.last || lastId;
            if (messages.length > 0 && atBottom) {
                scroller.scrollTop = scroller.scrollHeight;
            }
        };
        request.onerror = request.ontimeout = function () {
            polling = false;
            networkOk = false;
            paintDots();
        };
        advanceDots(); // one dot step per real request
        request.send();
    }

    poll();
    setInterval(poll, POLL_MS);
})();
