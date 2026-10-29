/* ===== Trang cau hinh WiFi cua tu rua xe =====
 * Goi API cua tu (net_wifi_portal.c):
 *   GET  /api/status   trang thai mang, WiFi, IP, may chu
 *   GET  /api/scan     danh sach WiFi xung quanh
 *   POST /api/connect  {"ssid","password"}  ket noi WiFi
 *   POST /api/forget   {"ssid"}             ngat + quen WiFi dang dung
 * Viet kieu ES5 (var, function) de chay ca tren trinh duyet captive portal doi cu. */
(function () {
  "use strict";

  var $ = function (id) { return document.getElementById(id); };

  var busy = false;   /* dang ket noi / ngat -> khong cho thao tac khac */
  var cur = null;     /* trang thai /api/status gan nhat */
  var lastAps = null; /* ket qua quet gan nhat (ve lai danh sach khi doi mang dang dung) */
  var inUseKey = "";  /* ten WiFi dang dung luc ve danh sach */

  var NET = { wifi: "WiFi", eth: "Ethernet", "4g": "4G", none: "Chưa có mạng" };

  var LOCK_ICON =
    '<svg width="13" height="13" viewBox="0 0 16 16" aria-label="Có mật khẩu"><path fill="currentColor" ' +
    'd="M4 7V5a4 4 0 1 1 8 0v2h.5A1.5 1.5 0 0 1 14 8.5v5a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 2 13.5v-5' +
    'A1.5 1.5 0 0 1 3.5 7H4zm2 0h4V5a2 2 0 1 0-4 0v2z"/></svg>';

  /* ================= GOI API ================= */

  function api(path, body) {
    var ctl = new AbortController();
    var timer = setTimeout(function () { ctl.abort(); }, path === "/api/scan" ? 12000 : 5000);
    var opt = body
      ? { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body), signal: ctl.signal }
      : { signal: ctl.signal };
    return fetch(path, opt).then(function (r) {
      clearTimeout(timer);
      if (!r.ok) throw r.status;
      return r.json();
    });
  }

  /* ================= TIEN ICH HIEN THI ================= */

  function esc(s) {
    return String(s).replace(/[&<>"']/g, function (c) { return "&#" + c.charCodeAt(0) + ";"; });
  }

  /* RSSI -> 1..4 vach song */
  function signalLevel(rssi) {
    return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  }

  function barsHtml(rssi) {
    var n = signalLevel(rssi);
    var h = '<span class="bars" aria-label="Sóng ' + n + '/4">';
    for (var i = 1; i <= 4; i++) h += "<i" + (i <= n ? ' class="f"' : "") + "></i>";
    return h + "</span>";
  }

  var msgTimer = null;

  /* cls: "ok" | "err" | "inf". hideMs > 0: tu an sau chung ay ms */
  function showMsg(cls, title, text, hideMs) {
    var m = $("msg");
    clearTimeout(msgTimer);
    m.className = "msg " + cls;
    m.innerHTML = (title ? "<b>" + esc(title) + "</b>" : "") + esc(text || "");
    if (cls) m.scrollIntoView({ block: "nearest" });
    if (hideMs) msgTimer = setTimeout(clearMsg, hideMs);
  }

  function clearMsg() {
    clearTimeout(msgTimer);
    $("msg").className = "msg";
  }

  /* ================= TRANG THAI TU ================= */

  function showStatus(s) {
    cur = s;
    $("mac").textContent = (s.mac || "").toUpperCase().replace(/(..)(?!$)/g, "$1:");
    $("hw").textContent = s.hw || "—";
    $("fw").textContent = s.fw || "—";

    /* Trang thai Internet tung mang: cham xanh = co ket noi */
    var links = s.links || {};
    ["eth", "4g", "wifi"].forEach(function (k) {
      $("l-" + k).className = "dot " + (links[k] ? "on" : "off");
    });

    var n = s.net || "none";
    $("s-net").innerHTML = '<span class="dot ' + (n === "none" ? "off" : "on") + '"></span>' + esc(NET[n] || n);
    $("s-wifi").textContent = s.ssid ? s.ssid + (s.connected ? "" : " (chưa kết nối)") : "Chưa cài đặt";
    $("s-ip").textContent = s.net_ip || "—"; /* IP cua mang dang dung (Ethernet / WiFi / 4G) */
    $("s-srv").innerHTML = '<span class="dot ' + (s.server ? "on" : "off") + '"></span>' +
                           (s.server ? "Đã kết nối" : "Chưa kết nối");

    /* Tu tu doi WiFi (vd ngat mang nay, tu vao mang da luu khac) -> ve lai de cap nhat "Dang dung" */
    if (lastAps && !busy && wifiInUse() !== inUseKey) renderList(lastAps);
  }

  function wifiInUse() {
    return cur && cur.connected && cur.ssid ? cur.ssid : "";
  }

  /* ================= DANH SACH WIFI ================= */

  /* Form duoi moi dong: mang dang dung -> nut Ngat ket noi; mang khac -> mat khau + Ket noi */
  function formHtml(ap, i, inUse) {
    if (inUse) {
      return '<button class="btn full danger off" type="button">Ngắt kết nối</button>';
    }
    var pw = ap.auth
      ? '<label for="pw' + i + '">Mật khẩu</label>' +
        '<div class="pw"><input id="pw' + i + '" type="password" autocomplete="off" maxlength="64">' +
        '<button class="eye" type="button" data-for="pw' + i + '">Hiện</button></div>'
      : "<label>Mạng không có mật khẩu</label>";
    return pw + '<button class="btn full go" type="button">Kết nối</button>';
  }

  function renderList(aps) {
    /* Gop cac AP trung ten (giu song manh nhat), sap song manh truoc */
    var best = {}, list = [];
    aps.forEach(function (a) {
      if (!a.ssid) return;
      if (!best[a.ssid] || best[a.ssid].rssi < a.rssi) best[a.ssid] = a;
    });
    for (var k in best) list.push(best[k]);
    lastAps = aps;
    inUseKey = wifiInUse();
    /* Mang dang dung len dau, con lai sap song manh truoc */
    list.sort(function (a, b) {
      var ua = a.ssid === inUseKey ? 1 : 0, ub = b.ssid === inUseKey ? 1 : 0;
      return ub - ua || b.rssi - a.rssi;
    });

    if (!list.length) {
      $("list").innerHTML = '<li class="empty">Không tìm thấy mạng WiFi nào. Bấm “Quét lại”.</li>';
      return;
    }

    $("list").innerHTML = list.map(function (a, i) {
      var inUse = inUseKey !== "" && a.ssid === inUseKey;
      return '<li data-i="' + i + '">' +
               '<button class="row" type="button">' +
                 '<span class="nm">' + esc(a.ssid) + (inUse ? '<span class="cur">Đang dùng</span>' : "") + "</span>" +
                 '<span class="ic">' + (a.auth ? LOCK_ICON : "") + barsHtml(a.rssi) + "</span>" +
               "</button>" +
               '<div class="fm">' + formHtml(a, i, inUse) + "</div>" +
             "</li>";
    }).join("");
    $("list").aps = list;
  }

  function apOf(el) {
    return $("list").aps[+el.closest("li").dataset.i];
  }

  function scan() {
    if (busy) return;
    var btn = $("scan");
    btn.disabled = true;
    btn.innerHTML = '<span class="spin"></span>';
    api("/api/scan")
      .then(renderList)
      .catch(function () {
        $("list").innerHTML = '<li class="empty">Không quét được. Thử “Quét lại”.</li>';
      })
      .then(function () {
        btn.disabled = false;
        btn.textContent = "Quét lại";
      });
  }

  /* ================= KET NOI ================= */

  function connect(ssid, pw, btn) {
    if (busy) return;
    if (!ssid) { showMsg("err", "Thiếu tên WiFi", ""); return; }
    if (pw && pw.length < 8) { showMsg("err", "Mật khẩu quá ngắn", "Mật khẩu WiFi tối thiểu 8 ký tự."); return; }

    busy = true;
    var oldText = btn.textContent;
    btn.disabled = true;
    btn.innerHTML = '<span class="spin"></span>&nbsp; Đang kết nối…';
    showMsg("inf", "Đang kết nối “" + ssid + "”…",
            "Có thể mất tới 20 giây. Điện thoại có thể tự rớt WiFi của tủ trong lúc này, chờ một chút rồi kết nối lại.");

    var t0 = Date.now();
    var FAIL_TEXT = "Kiểm tra tên, mật khẩu và vị trí phát WiFi rồi thử lại.";

    function done() {
      busy = false;
      btn.disabled = false;
      btn.textContent = oldText;
    }

    function poll() {
      api("/api/status").then(function (s) {
        showStatus(s);
        if (s.ssid === ssid && s.connected && s.ip) {
          done();
          showMsg("ok", "Đã kết nối “" + ssid + "”",
                  "IP Address: " + s.ip + (s.server ? ". Tủ đã kết nối máy chủ." : ". Đang kết nối máy chủ…"));
          scan();
          return;
        }
        if (s.err === "AUTH_FAIL") { done(); showMsg("err", "Sai mật khẩu", "Kiểm tra lại mật khẩu WiFi “" + ssid + "”."); return; }
        if (s.err === "NO_AP") { done(); showMsg("err", "Không tìm thấy mạng", "WiFi “" + ssid + "” không có trong vùng phủ sóng."); return; }
        if (s.err === "FAIL") { done(); showMsg("err", "Không kết nối được", FAIL_TEXT); return; }
        next();
      }).catch(next);
    }

    function next() {
      if (Date.now() - t0 > 25000) { done(); showMsg("err", "Không kết nối được", FAIL_TEXT); return; }
      setTimeout(poll, 1500);
    }

    api("/api/connect", { ssid: ssid, password: pw || "" })
      .then(function (r) {
        if (r && r.ok === false) { done(); showMsg("err", "Không kết nối được", r.msg || ""); return; }
        setTimeout(poll, 2000);
      })
      .catch(function () { setTimeout(poll, 2000); });
  }

  /* ================= NGAT + QUEN MANG DANG DUNG =================
   * Bam 1 lan la ngat (hau qua da ghi san tren form). */

  function forget(ssid, btn) {
    if (busy) return;
    busy = true;
    btn.disabled = true;
    btn.innerHTML = '<span class="spin"></span>&nbsp; Đang ngắt…';
    var t0 = Date.now();

    function done() {
      busy = false;
      btn.disabled = false;
      btn.textContent = "Ngắt kết nối";
    }

    function poll() {
      api("/api/status").then(function (s) {
        showStatus(s);
        if (!(s.connected && s.ssid === ssid)) {
          done();
          var net = s.net && s.net !== "none" ? NET[s.net] || s.net : "";
          if (net) {
            showMsg("ok", "Đã ngắt WiFi “" + ssid + "”",
                    "Tủ đang dùng " + net + (s.server ? ", đã kết nối máy chủ." : ", đang kết nối lại máy chủ…"), 3000);
          } else {
            showMsg("err", "Đã ngắt WiFi “" + ssid + "”",
                    "Tủ hiện không có mạng. Chọn một mạng WiFi bên dưới để kết nối lại.", 3000);
          }
          scan();
          return;
        }
        next();
      }).catch(next);
    }

    function next() {
      if (Date.now() - t0 > 15000) { done(); showMsg("err", "Chưa ngắt được", "Thử lại sau vài giây."); return; }
      setTimeout(poll, 1000);
    }

    api("/api/forget", { ssid: ssid })
      .then(function (r) {
        if (r && r.ok === false) { done(); showMsg("err", "Không ngắt được", r.msg || ""); return; }
        setTimeout(poll, 1000);
      })
      .catch(function () { setTimeout(poll, 1000); });
  }

  /* ================= SU KIEN ================= */

  document.addEventListener("click", function (e) {
    /* Hien / an mat khau */
    var eye = e.target.closest(".eye");
    if (eye) {
      var input = $(eye.dataset.for);
      input.type = input.type === "password" ? "text" : "password";
      eye.textContent = input.type === "password" ? "Hiện" : "Ẩn";
      return;
    }

    /* Bam vao 1 mang: mo / dong form (chi mo 1 form mot luc) */
    var row = e.target.closest(".row");
    if (row) {
      var li = row.parentNode;
      var wasOpen = li.classList.contains("open");
      [].forEach.call(document.querySelectorAll("#list li.open"), function (x) { x.classList.remove("open"); });
      if (!wasOpen) {
        li.classList.add("open");
        var pw = li.querySelector("input");
        if (pw) pw.focus();
      }
      clearMsg();
      return;
    }

    /* Ngat ket noi */
    var off = e.target.closest(".off");
    if (off) { forget(apOf(off).ssid, off); return; }

    /* Ket noi */
    var go = e.target.closest(".go");
    if (go) {
      var pwInput = go.closest("li").querySelector("input");
      connect(apOf(go).ssid, pwInput ? pwInput.value : "", go);
    }
  });

  /* Enter trong o mat khau = bam Ket noi */
  document.addEventListener("keydown", function (e) {
    if (e.key !== "Enter" || e.target.tagName !== "INPUT") return;
    var li = e.target.closest("li");
    if (li) {
      var go = li.querySelector(".go");
      if (go) go.click();
    } else {
      $("h-go").click();
    }
  });

  $("scan").onclick = scan;
  $("h-go").onclick = function () {
    connect($("h-ssid").value.trim(), $("h-pw").value, this);
  };

  /* ================= KHOI DONG ================= */

  api("/api/status")
    .then(showStatus)
    .catch(function () {})
    .then(scan);

  /* Cap nhat trang thai moi 5 giay */
  setInterval(function () {
    if (!busy) api("/api/status").then(showStatus).catch(function () {});
  }, 5000);
})();