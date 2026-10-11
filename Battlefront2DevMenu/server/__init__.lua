local HttpServer = require "server/http_server"

local PORT <const> = 29472
local PRIVATE_MODE = os.getenv("KYBER_DEV_MODE") ~= nil or os.getenv("BF2_PRIVATE_DEV_MENU") ~= nil
local TOKEN = os.getenv("BF2_DEV_TOKEN") or ""

local function reply(req, status, text)
    req:send(status, text)
end

if not PRIVATE_MODE then
    print("[Battlefront2DevMenu] Private mode is not enabled. Server bridge disabled.")
    return
end

if TOKEN == "" then
    print("[Battlefront2DevMenu] BF2_DEV_TOKEN is missing. Server bridge disabled.")
    return
end

local function authorized(req)
    return (req.headers["x-bf2-dev-token"] or "") == TOKEN
end

local function handleRequest(req)
    if not authorized(req) then
        reply(req, "403 Forbidden", "forbidden")
        return
    end

    if req.method == "GET" and req.path == "/bf2dev/health" then
        reply(req, "200 OK", "ok")
        return
    end

    if req.method == "POST" and req.path == "/bf2dev/force-start" then
        -- This is the same Frostbite console path Kyber exposes through
        -- Console.Execute. The supplied Frosty package also contains the
        -- built-in Whiteshark.SkipLobby command in PF_GameMode_Controller.
        Console.Execute("Whiteshark.SkipLobby")
        reply(req, "200 OK", "Whiteshark.SkipLobby queued")
        print("[Battlefront2DevMenu] Force Start Lobby requested")
        return
    end

    reply(req, "404 Not Found", "not found")
end

local server = HttpServer.new(PORT, handleRequest)
if server == nil then
    print("[Battlefront2DevMenu] Failed to initialize private bridge")
end
