HttpRequest = {}
HttpRequest.__index = HttpRequest

function HttpRequest.new(server, client, method, path, headers, body)
    local obj = setmetatable({}, HttpRequest)
    obj.server = server
    obj.client = client
    obj.method = method
    obj.path = path
    obj.headers = headers
    obj.body = body
    return obj
end

function HttpRequest:send(status, data)
    data = data or ""
    self.client:Send("HTTP/1.1 " .. status .. "\r\nContent-Length: " .. #data .. "\r\nConnection: close\r\n\r\n" .. data)
    self.server:removeClient(self.client)
end

local function parseHttpRequest(request)
    local method, path, version = request:match("^(%S+)%s(%S+)%s(HTTP/%d%.%d)\r?\n")
    if not method or not path or not version then
        return nil
    end

    local headers = {}
    local bodyStart = request:find("\r\n\r\n")
    local headerEnd = bodyStart and bodyStart - 1 or #request
    local headersPart = request:sub(#method + #path + #version + 4, headerEnd)

    for line in headersPart:gmatch("[^\r\n]+") do
        local key, value = line:match("^(.-):%s*(.*)$")
        if key and value then
            headers[key:lower()] = value
        end
    end

    return {
        method = method,
        path = path,
        headers = headers,
        body = bodyStart and request:sub(bodyStart + 4) or ""
    }
end

HttpServer = {}
HttpServer.__index = HttpServer

function HttpServer.new(port, requestCallback)
    local obj = setmetatable({}, HttpServer)
    obj.socket = SocketManager.Create(port)
    obj.clients = {}
    obj.requestCallback = requestCallback
    if obj.socket == nil then
        print("[Battlefront2DevMenu] Failed to create local bridge socket")
        return nil
    end

    EventManager.Listen("Server:UpdatePre", function(self, deltaSecs)
        self:update(deltaSecs)
    end, obj)

    print("[Battlefront2DevMenu] Private bridge listening on port " .. port)
    return obj
end

function HttpServer:update(deltaSecs)
    local client = self.socket:Accept()
    if client then
        table.insert(self.clients, client)
    end

    for i = #self.clients, 1, -1 do
        local c = self.clients[i]
        local data = c:Recv(4096)
        if data == nil then
            goto continue
        end

        if #data == 0 then
            self:removeClient(c)
            goto continue
        end

        local parsed = parseHttpRequest(data)
        if parsed ~= nil then
            self.requestCallback(HttpRequest.new(self, c, parsed.method, parsed.path, parsed.headers, parsed.body))
        else
            HttpRequest.new(self, c, "", "", {}, ""):send("400 Bad Request", "bad request")
        end

        ::continue::
    end
end

function HttpServer:removeClient(client)
    client:Close()
    for i = #self.clients, 1, -1 do
        if self.clients[i] == client then
            table.remove(self.clients, i)
            return
        end
    end
end

return HttpServer
