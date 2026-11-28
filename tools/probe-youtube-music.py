#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check one anonymous YouTube Music playback response and direct audio range."""

import html
import http.client
import http.cookiejar
import json
import re
import signal
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


MUSIC_ORIGIN = "https://music.youtube.com"
# Monkeys Spinning Monkeys — Kevin MacLeod (incompetech.com), CC BY 4.0.
# https://incompetech.com/music/royalty-free/index.html?isrc=USUAN1400011
VIDEO_ID = "2eZVbrO6Z1M"
WATCH_URL = f"{MUSIC_ORIGIN}/watch?v={VIDEO_ID}"
USER_AGENT = "Swiftify-YouTube-Music-Probe/1.0"
RANGE_SIZE = 4096


class ProbeError(Exception):
    pass


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def fetch(opener, url, stage, size_limit, *, headers=None, data=None, status=200):
    request = urllib.request.Request(
        url,
        headers={"User-Agent": USER_AGENT, "Accept-Encoding": "identity", **(headers or {})},
        data=data,
    )
    started = time.monotonic()
    try:
        with opener.open(request, timeout=8) as response:
            if response.status != status:
                raise ProbeError(f"{stage} returned HTTP {response.status}, expected {status}")
            body = response.read(size_limit + 1)
            response_headers = response.headers
    except urllib.error.HTTPError as error:
        with error:
            if 300 <= error.code < 400:
                raise ProbeError(f"{stage} returned HTTP {error.code}; redirect was stopped") from None
            raise ProbeError(f"{stage} returned HTTP {error.code}") from None
    except urllib.error.URLError as error:
        tunnel = re.search(r"Tunnel connection failed: ([0-9]{3})", str(error.reason))
        if tunnel:
            raise ProbeError(f"{stage} proxy CONNECT failed with HTTP {tunnel[1]}") from None
        raise ProbeError(f"{stage} could not establish a connection") from None
    except (http.client.HTTPException, OSError):
        raise ProbeError(f"{stage} could not read the response") from None
    if len(body) > size_limit:
        raise ProbeError(f"{stage} exceeded its response size limit")
    elapsed_ms = round((time.monotonic() - started) * 1000)
    print(f"{stage} returned HTTP {status}, {len(body)} bytes in {elapsed_ms} ms.")
    return body, response_headers


def extract_config(page):
    config = {}
    decoder = json.JSONDecoder()
    for entry in re.finditer(r"\bytcfg\.set\s*\(\s*(?=\{)", page):
        try:
            values, end = decoder.raw_decode(page, entry.end())
        except ValueError:
            continue
        if page[end:].lstrip().startswith(")"):
            config.update(values)
    if config.get("LOGGED_IN") is True:
        raise ProbeError("YouTube Music bootstrap unexpectedly reported a signed-in session")
    client = config.get("INNERTUBE_CONTEXT", {}).get("client", {})
    version = client.get("clientVersion")
    if client.get("clientName") != "WEB_REMIX" or not isinstance(version, str) or not re.fullmatch(
        r"[0-9.]{4,32}", version
    ):
        return None
    return config, client


def bootstrap_blocker(page):
    blocked_pages = (
        ("a consent interstitial", (
            "before you continue to youtube", "consent.youtube.com", "consent.google.com",
        )),
        ("an admission challenge", (
            "our systems have detected unusual traffic", "g-recaptcha",
            "confirm you’re not a bot", "confirm you're not a bot",
        )),
        ("a sign-in requirement", ("sign in to continue to youtube music", "gaia_loginform")),
        ("a regional limitation", (
            "youtube music is not available in your area", "youtube music isn't available in your country",
        )),
        ("an unsupported-browser message", (
            "browser is not supported", "unsupported browser", "please update your browser",
        )),
    )
    for reason, markers in blocked_pages:
        if any(marker in page for marker in markers):
            return reason
    if re.search(r'<meta\s+[^>]*http-equiv\s*=\s*["\']?refresh\b', page):
        return "a client-side redirection page"
    return None


def load_guest_config(guest):
    headers = {"Accept": "text/html", "Accept-Language": "en-US,en;q=0.9"}
    entries = ((WATCH_URL, "Music watch bootstrap"), (f"{MUSIC_ORIGIN}/", "Music homepage bootstrap"))
    for url, stage in entries:
        body, response_headers = fetch(guest, url, stage, 4 * 1024 * 1024, headers=headers)
        if response_headers.get_content_type() != "text/html":
            raise ProbeError(f"{stage} did not return HTML")
        page = body.decode("utf-8", errors="replace")
        if guest_config := extract_config(page):
            return guest_config
        lower_page = page.casefold()
        if blocker := bootstrap_blocker(html.unescape(lower_page)):
            raise ProbeError(f"{stage} indicated {blocker}")
        if url == WATCH_URL and re.search(r"<ytmusic-app(?:\s|>)", lower_page):
            print("Music watch page returned an application shell without guest configuration. Checking its homepage once.")
            continue
        raise ProbeError(f"{stage} returned an unrecognized HTML page without a valid WEB_REMIX client version")


def is_https_host(parsed, hostname):
    return parsed.scheme == "https" and parsed.netloc == hostname and not parsed.fragment


def player_timestamp(config, anonymous):
    stamp = str(config.get("STS", ""))
    if re.fullmatch(r"[0-9]{5,10}", stamp):
        return int(stamp)
    script = config.get("PLAYER_JS_URL")
    if not script:
        clients = config.get("WEB_PLAYER_CONTEXT_CONFIGS", {})
        urls = {client.get("jsUrl") for client in clients.values()} - {None, ""}
        if len(urls) == 1:
            script = urls.pop()
    if not script:
        return None
    if not re.fullmatch(r"[!-~]+", script):
        raise ProbeError("YouTube Music bootstrap proposed an invalid player script URL")
    url = urllib.parse.urljoin(MUSIC_ORIGIN, script)
    parsed = urllib.parse.urlsplit(url)
    if (
        not any(is_https_host(parsed, host) for host in ("music.youtube.com", "www.youtube.com"))
        or not re.fullmatch(r"/s/player/[A-Za-z0-9_./-]{1,500}\.js", parsed.path)
        or ".." in parsed.path.split("/")
    ):
        raise ProbeError("YouTube Music bootstrap proposed a player script outside the allowlist")
    source, _ = fetch(anonymous, url, "Public player script", 8 * 1024 * 1024)
    # Read version metadata as data. No frontend script is executed.
    stamps = set(re.findall(rb'["\']?\b(?:signatureTimestamp|sts)["\']?\s*:\s*([0-9]{5,10})\b', source))
    return int(stamps.pop()) if len(stamps) == 1 else None


def player_response(music, bootstrap_client, stamp):
    client = {key: bootstrap_client[key] for key in ("clientName", "clientVersion")}
    for key in ("hl", "gl", "visitorData"):
        value = bootstrap_client.get(key)
        if isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9_%.=+/-]{1,2048}", value):
            client[key] = value
    headers = {
        "Accept": "application/json",
        "Content-Type": "application/json",
        "Origin": MUSIC_ORIGIN,
        "Referer": WATCH_URL,
        "X-YouTube-Client-Name": "67",
        "X-YouTube-Client-Version": client["clientVersion"],
    }
    if "visitorData" in client:
        headers["X-Goog-Visitor-Id"] = client["visitorData"]
    playback = {"html5Preference": "HTML5_PREF_WANTS"}
    if stamp is not None:
        playback["signatureTimestamp"] = stamp
    body = {
        "context": {"client": client, "user": {}},
        "videoId": VIDEO_ID,
        "playbackContext": {"contentPlaybackContext": playback},
    }
    data, _ = fetch(
        music, f"{MUSIC_ORIGIN}/youtubei/v1/player?prettyPrint=false", "Music player",
        2 * 1024 * 1024, headers=headers, data=json.dumps(body).encode("utf-8"),
    )
    return json.loads(data)


def direct_audio(response):
    playability = response.get("playabilityStatus", {})
    status = playability.get("status")
    if status != "OK":
        known_statuses = {"ERROR", "UNPLAYABLE", "LOGIN_REQUIRED", "CONTENT_CHECK_REQUIRED", "AGE_CHECK_REQUIRED"}
        status = status if status in known_statuses else "unknown"
        reason = playability.get("reason")
        message = "The page needs to be reloaded." if reason == "The page needs to be reloaded." else ""
        suffix = f" {message}" if message else ""
        raise ProbeError(f"YouTube Music declined playback ({status}).{suffix}")
    streaming = response.get("streamingData", {})
    formats = streaming.get("adaptiveFormats", []) + streaming.get("formats", [])
    for media_type in ("audio/webm", "audio/mp4", "audio/ogg"):
        for audio_format in formats:
            url = audio_format.get("url")
            if not url or audio_format.get("mimeType", "").split(";", 1)[0] != media_type:
                continue
            if (
                audio_format.get("signatureCipher")
                or audio_format.get("cipher")
                or not re.fullmatch(r"[!-~]+", url)
            ):
                continue
            parsed = urllib.parse.urlsplit(url)
            host = parsed.hostname or ""
            if (
                re.fullmatch(r"[a-z0-9][a-z0-9-]{0,62}\.googlevideo\.com", host)
                and is_https_host(parsed, host)
                and parsed.path == "/videoplayback"
            ):
                return url, media_type
    raise ProbeError("YouTube Music offered no direct supported audio URL on the allowlist")


def verify_range(anonymous, url, mime_type):
    data, headers = fetch(
        anonymous, url, "Direct audio range", RANGE_SIZE,
        headers={"Range": f"bytes=0-{RANGE_SIZE - 1}"}, status=206,
    )
    content_range = re.fullmatch(r"bytes 0-([0-9]+)/([0-9]+)", headers.get("Content-Range", ""))
    if not content_range or int(content_range[1]) + 1 != len(data) or int(content_range[2]) < len(data):
        raise ProbeError("Direct audio response did not match its requested byte range")
    magic = {
        "audio/webm": data.startswith(b"\x1a\x45\xdf\xa3"),
        "audio/mp4": data[4:8] == b"ftyp",
        "audio/ogg": data.startswith(b"OggS"),
    }
    if not magic[mime_type]:
        raise ProbeError("Direct audio bytes did not match the advertised container")
    print(f"Verified {mime_type} container bytes from YouTube Music via its CDN.")


def deadline(_signum, _frame):
    raise ProbeError("The probe exceeded its total time limit")


def main():
    if hasattr(signal, "SIGALRM"):
        signal.signal(signal.SIGALRM, deadline)
        signal.alarm(40)
    try:
        # Only the first-party Music session receives cookies set during this run.
        guest = urllib.request.build_opener(NoRedirect(), urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        anonymous = urllib.request.build_opener(NoRedirect())
        config, client = load_guest_config(guest)
        stamp = player_timestamp(config, anonymous)
        availability = "available" if stamp is not None else "unavailable"
        print(f"Music guest uses WEB_REMIX {client['clientVersion']}, public player release metadata {availability}.")
        response = player_response(guest, client, stamp)
        url, mime_type = direct_audio(response)
        verify_range(anonymous, url, mime_type)
    except ProbeError as error:
        message = str(error).removesuffix(".")
        print(f"Direct YouTube Music audio remains unverified. {message}.")
        return 1
    except (AttributeError, TypeError, ValueError):
        print("Direct YouTube Music audio remains unverified. Unexpected provider response format.")
        return 1
    else:
        return 0
    finally:
        if hasattr(signal, "SIGALRM"):
            signal.alarm(0)
        print("Native audio engine playback requires a separate test.")


if __name__ == "__main__":
    sys.exit(main())
