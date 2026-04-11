


```cpp

// TLSBind extends TCPBind

struct TLSCert {
    String data;
    String type;
}

tls.ca: Array<TLSCert>; // Automatically starts with resolv.ca
tls.cert: TLSCert; // My cert assoc with those RSA
tls.privateRSAKey: 
tls.publicRSAKey: 
tls.load(pemString, "pem");


tls.onPacket([]() {

});





// onConnect, onDisconnect, and so on...



// HTTPBind extends TLSBind
HTTPBind http(80);



http.onPacket([](Client cli, String chunk) {

    cli.headers: Map<String, String>; // Sent headers as of now
    cli.began; // Did it begin the body
    cli.end; // Did it close the connection

    cli.method: String;
    cli.url: String;
    cli.status: i64;
    cli.statusMessage: String;

    Regex rgx(".*/hello/(.*)/regex/is/allowed");
    RegexMatch mtch = rgx.match(cli.url);
    if(mtch.found && cli.end) {
        // Handle the request here...
    }

    // This here will fire each time a chunk is received so
    // Use cli.end to start processing, cli.began to know what to do... etc
    

    http.send(cli, "Hello " + rgx.match[1]);
    http.destroy(cli); // Sends end, and forgets everything, forgets tls session keys, everything

    return;
});

// Other side

HTTPBind client(); // Port 0 means random port

http.fetch(cli, "https://example.com"); // This wraps send, send the request, url, also sets cli to the correct destination, if address was fully numerical, it just uses it, otherwise it resolves it using DNSResolver Singleton...
http.method("GET");
// Or http.get, convenience
http.header(cli, "Content-Type", "application/json"); // Just sends arg[0]: arg[1]
http.header(cli, Map<String, String>); // Sends a whole bunch of headers...
http.status(cli, 200, "Hello"); // Just wraps send too
http.begin(cli); // Just sends newlines to indicate body begin
http.send(cli, "... Hello .. Hi! ");

// DNSResolver: HTTPBind (DoH)
// This caches also...
DNSResolver resolv; // Singleton!
resolv.ca; // Here the user fills those, and automatically any new bind will have them.
resolv.resolve("https://example.com:80/ddd"); // https://1.2.3.4:80/ddd



```

With no external flags, HTTPBind and TLSBind both serve as client and server, with only thing that determines is who starts, I mean, no new flags




```cpp


// LLT/Compression
Compression comp; // Learns in the dictionary sense.
comp.maxScratch = 1024 * 1024 * 128; // 128MBs
comp.dictionary = Array<String>
result = comp.decompress(something);
result = comp.compress(something);

// LLT/Deflate (miniz)
DEFLATE dfl; // extends Compression

// LLT/LZ4 (lz4)
LZ4 lz; // extends Compression

// LLT/Zstd (libzstd)
ZSTD zs; // extends Compression




Archive arch; // Extends filesystem device, has all fs things
// It is a full virtual filesystem, keeps a cache of things in ram.

arch.maxCache = 1024 * 1024; // 1 MB, keeps it in ram, forgets old files, 0 means keeps nothing

ZIPArchive zip; // extends Archive

zip.onFormatRequest([](u64 position, u64 length) {
    // This is called when autoload is not NONE, and theres no entry in cache
});

zip.formatSize();
zip.formatCompressed();
zip.formatRead(u64 position, u64 length): String buffer; // This allows us to save the zip.


```