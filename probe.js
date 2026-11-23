// Close-URL size probe (beta diagnostic, standalone page).
//
// Why: the phone silently truncates the pebblejs://close# response, and every
// decision about how routines can be delivered depends on where that limit
// actually is. The only figure we have is a comment in index_dev.html saying
// 279 encoded characters deliver and 672 do not — a guess in between. This page
// measures it: point configUrlDev at probe.html, open Settings on the watch, tap
// a size, and read "webviewclosed: raw response length = N" in pebble logs.
//
// The payloads are pure ASCII, so their percent-encoded length equals their raw
// length and the number pkjs reports can be compared with the button directly.

function buildProbePayload(targetLen) {
    var overhead = JSON.stringify({ d: '' }).length;   // {"d":""} = 8 chars
    var filler = new Array(Math.max(0, targetLen - overhead) + 1).join('A');
    return JSON.stringify({ d: filler });
}

function probeReturnTo() {
    // Honour ?return_to=… so the page also works when opened in a plain browser.
    var m = /[?&]return_to=([^&]*)/.exec(window.location.search);
    if (m) { try { return decodeURIComponent(m[1]); } catch (e) {} }
    return 'pebblejs://close#';
}

function closeWith(json, label) {
    var encoded = encodeURIComponent(json);
    var status = document.getElementById('status');
    if (status) {
        status.textContent = 'Closing: ' + label + ' → JSON ' + json.length +
                             ' chars, encoded ' + encoded.length + ' chars';
    }
    // A short delay so the status is painted before navigation starts.
    setTimeout(function () {
        window.location.href = probeReturnTo() + encoded;
    }, 60);
}

// A routine serialised exactly like serialiseRoutine() in index_dev.html:
//   Name|Exercise|sets|reps|weight|modifier|note|...
function serialiseRoutine(name, exercises) {
    var s = name;
    for (var i = 0; i < exercises.length; i++) {
        var e = exercises[i];
        s += '|' + e[0] + '|' + e[1] + '|' + e[2] + '|' + e[3] + '|' +
             (e[4] || 0) + '|' + (e[5] && e[5].trim() ? e[5].trim() : '-');
    }
    return s;
}

// Short, realistic exercise names: the common case.
function shortRoutine(count) {
    var names = ['Squat', 'Bench Press', 'Barbell Row', 'Overhead Press',
                 'Curl', 'Lateral Raise', 'Dip', 'Leg Press', 'Calf Raise',
                 'Hamstring Curl', 'Fly', 'Pushdown', 'Plank', 'Lunge', 'Shrug'];
    var ex = [];
    for (var i = 0; i < count; i++) ex.push([names[i % names.length], 3, 10, 40, 0, '-']);
    return serialiseRoutine('Probe ' + count, ex);
}

// Worst case: long names plus a note on every exercise.
function longRoutine(count) {
    var ex = [];
    for (var i = 0; i < count; i++) {
        ex.push(['Dumbbell Incline Bench Press ' + (i + 1), 4, 8, 32, 0, 'seat at 4, pin at 3']);
    }
    return serialiseRoutine('Probe Long ' + count, ex);
}

(function init() {
    var sizes = [100, 200, 279, 350, 450, 550, 672, 800, 1000, 1200, 1500, 2000];
    var host = document.getElementById('sizes');
    sizes.forEach(function (n) {
        var b = document.createElement('button');
        b.textContent = n;
        b.onclick = function () { closeWith(buildProbePayload(n), 'raw ' + n); };
        host.appendChild(b);
    });

    var rhost = document.getElementById('routines');
    [3, 6, 10, 15].forEach(function (n) {
        var b = document.createElement('button');
        b.className = 'big';
        b.textContent = n + ' ex';
        b.onclick = function () {
            var r = shortRoutine(n);
            closeWith(JSON.stringify({ cmd: 'save', name: 'Probe ' + n, routine: r }),
                      n + ' exercises (' + r.length + ' raw)');
        };
        rhost.appendChild(b);
    });

    var lb = document.createElement('button');
    lb.className = 'big';
    lb.textContent = '15 ex + notes';
    lb.onclick = function () {
        var r = longRoutine(15);
        closeWith(JSON.stringify({ cmd: 'save', name: 'Probe Long 15', routine: r }),
                  '15 exercises with notes (' + r.length + ' raw)');
    };
    rhost.appendChild(lb);

    document.getElementById('status2').textContent =
        '15 exercises, short names = ' + shortRoutine(15).length + ' raw chars; ' +
        '15 exercises with notes = ' + longRoutine(15).length + ' raw chars.';
    console.log('probe ready: short15=' + shortRoutine(15).length +
                ' long15=' + longRoutine(15).length);
})();
