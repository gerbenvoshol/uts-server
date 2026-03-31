Deploy
======

This page covers everything needed to take uts-server from a compiled binary to a
fully-operational, production-grade Time-Stamp Authority.

Usage
-----

.. sourcecode:: bash

    $ ./uts-server --help
    Usage: uts-server [OPTION...] -c CONFFILE [-d] [-D] [-p <pidfile>]

    UTS micro timestamp server (RFC 3161)

      -c, --conffile=CONFFILE    Path to configuration file
      -d, --daemonize            Launch as a daemon
      -D, --debug                STDOUT debugging
      -p, --pidfile=PIDFILE      Path to pid file
      -?, --help                 Give this help list
          --usage                Give a short usage message
      -V, --version              Print program version

    Mandatory or optional arguments to long options are also mandatory or optional
    for any corresponding short options.

    Report bugs to Pierre-Francois Carpentier <carpentier.pf@gmail.com>.

Running uts-server
------------------

To debug problems with uts-server, run it in the foreground in debug mode:

.. sourcecode:: bash

    # Foreground, verbose debug output on stdout
    $ ./uts-server -c /etc/uts-server/uts-server.cnf -D

To run it as a daemon:

.. sourcecode:: bash

    # Daemon mode with a PID file
    $ ./uts-server -c /etc/uts-server/uts-server.cnf -d -p /var/run/uts-server/uts-server.pid

.. _pki-setup:

Setting up the PKI
------------------

uts-server requires two X.509 artefacts:

1. A **CA certificate** — used by clients to verify timestamp tokens.
2. A **TSA signing certificate + private key** — used by the server to sign tokens.

.. warning::

    The TSA signing certificate **must** have ``extendedKeyUsage = critical,timeStamping``
    and **no other** Extended Key Usage value.  OpenSSL will reject any certificate that
    does not satisfy this constraint.

Creating a self-signed TSA CA
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: bash

    # Create directories
    mkdir -p /etc/uts-server/pki/private
    chmod 700 /etc/uts-server/pki/private

    # Generate a 4096-bit RSA CA key
    openssl genrsa -out /etc/uts-server/pki/private/tsacakey.pem 4096

    # Self-sign a CA certificate (valid 10 years)
    openssl req -new -x509 -days 3650 \
        -key  /etc/uts-server/pki/private/tsacakey.pem \
        -out  /etc/uts-server/pki/tsaca.pem \
        -subj "/C=US/O=My Organisation/CN=TSA Root CA"

Creating the TSA signing certificate
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

First, write an OpenSSL extension configuration file that enforces the TSA EKU:

.. sourcecode:: ini

    # /etc/uts-server/pki/tsa_cert.ext
    [ tsa_cert ]
    basicConstraints       = CA:FALSE
    keyUsage               = nonRepudiation, digitalSignature
    extendedKeyUsage       = critical,timeStamping
    subjectKeyIdentifier   = hash
    authorityKeyIdentifier = keyid,issuer:always

Then generate the key and certificate:

.. sourcecode:: bash

    # Generate TSA signing key (2048-bit RSA is sufficient for a signing cert)
    openssl genrsa \
        -out /etc/uts-server/pki/private/tsakey.pem 2048
    chmod 400 /etc/uts-server/pki/private/tsakey.pem

    # Generate a Certificate Signing Request
    openssl req -new \
        -key  /etc/uts-server/pki/private/tsakey.pem \
        -out  /etc/uts-server/pki/tsa.csr \
        -subj "/C=US/O=My Organisation/CN=TSA Signing Certificate"

    # Sign the CSR with the CA, applying the timeStamping extension
    openssl x509 -req -days 730 \
        -in      /etc/uts-server/pki/tsa.csr \
        -CA      /etc/uts-server/pki/tsaca.pem \
        -CAkey   /etc/uts-server/pki/private/tsacakey.pem \
        -CAcreateserial \
        -extfile /etc/uts-server/pki/tsa_cert.ext \
        -extensions tsa_cert \
        -out     /etc/uts-server/pki/tsacert.pem

    # Verify the certificate has the correct EKU
    openssl x509 -in /etc/uts-server/pki/tsacert.pem -noout -text \
        | grep -A2 "Extended Key"
    # Expected output:
    #   X509v3 Extended Key Usage: critical
    #       Time Stamping

.. note::

    The CA private key (``tsacakey.pem``) is only needed for certificate operations.
    It should be stored offline or in a hardware token and **not** placed on the server.

Creating the configuration file
--------------------------------

Copy the example configuration and adapt it:

.. sourcecode:: bash

    cp /usr/share/doc/uts-server/uts-server.cnf /etc/uts-server/uts-server.cnf
    # or from the source tree:
    # cp conf/uts-server.cnf /etc/uts-server/uts-server.cnf

Edit ``/etc/uts-server/uts-server.cnf`` and set at minimum the ``[ tsa ]`` section:

.. sourcecode:: ini

    [ tsa ]
    dir             = /etc/uts-server/pki
    signer_cert     = $dir/tsacert.pem
    certs           = $dir/tsaca.pem
    signer_key      = $dir/private/tsakey.pem
    default_policy  = tsa_policy1
    other_policies  = tsa_policy2, tsa_policy3
    digests         = sha256, sha384, sha512
    accuracy        = secs:1, millisecs:500, microsecs:100
    clock_precision_digits = 0
    ordering        = yes
    tsa_name        = yes
    ess_cert_id_chain = no

.. tip::

    Omit weak algorithms (``md5``, ``sha1``) from ``digests`` in new deployments.
    Modern clients use SHA-256 or stronger.

Enabling HTTPS / TLS
--------------------

uts-server has built-in TLS support via civetweb.  Prepare a PEM file that contains
both the server private key and the server certificate (concatenated):

.. sourcecode:: bash

    # Combine server key and certificate into a single PEM file
    cat /path/to/server.key /path/to/server.crt > /etc/uts-server/server.pem
    chmod 400 /etc/uts-server/server.pem

Then update the ``[ main ]`` section of the configuration file:

.. sourcecode:: ini

    [ main ]
    # Listen on port 318 for plain HTTP and port 3161 for HTTPS
    listening_ports   = 318,3161s

    # Path to the PEM file containing the server key and certificate
    ssl_certificate   = /etc/uts-server/server.pem

    # Optional: minimum TLS version (4 = TLS 1.2 only)
    ssl_protocol_version = 4

    # Optional: restrict to strong cipher suites
    ssl_cipher_list = HIGH:!aNULL:!eNULL:!EXPORT:!RC4:!DES:!3DES:!MD5

.. note::

    RFC 3161 well-known ports are **318** (plain) and **3161** (TLS).
    Many deployments also use port **80/443** behind a reverse proxy.

Running as a non-root user
--------------------------

Running network services as root is a security risk.  To bind to a privileged port
(< 1024) without root, either:

* use a **reverse proxy** (nginx, HAProxy) on port 80/443 that forwards to
  uts-server on an unprivileged port, or
* grant the binary the ``CAP_NET_BIND_SERVICE`` Linux capability:

.. sourcecode:: bash

    # Create a dedicated system user and group
    useradd --system --no-create-home --shell /sbin/nologin uts-server

    # Set ownership on configuration and PKI files
    chown -R root:uts-server /etc/uts-server/
    chmod 750 /etc/uts-server/
    chmod 640 /etc/uts-server/uts-server.cnf
    chmod 400 /etc/uts-server/pki/private/tsakey.pem

    # Allow binding to privileged ports without root
    setcap 'cap_net_bind_service=+ep' /usr/sbin/uts-server

Then set ``run_as_user = uts-server`` in the ``[ main ]`` section:

.. sourcecode:: ini

    [ main ]
    run_as_user = uts-server

Running as a systemd service
-----------------------------

Create ``/etc/systemd/system/uts-server.service``:

.. sourcecode:: ini

    [Unit]
    Description=UTS RFC 3161 Time-Stamp Authority Server
    After=network.target
    Documentation=http://uts-server.readthedocs.org/

    [Service]
    Type=forking
    User=uts-server
    Group=uts-server
    PIDFile=/var/run/uts-server/uts-server.pid
    RuntimeDirectory=uts-server
    RuntimeDirectoryMode=0755
    ExecStart=/usr/sbin/uts-server \
        -c /etc/uts-server/uts-server.cnf \
        -d \
        -p /var/run/uts-server/uts-server.pid
    ExecReload=/bin/kill -HUP $MAINPID
    Restart=on-failure
    RestartSec=5
    # Harden the service
    NoNewPrivileges=yes
    PrivateTmp=yes
    ProtectSystem=strict
    ReadWritePaths=/var/run/uts-server /var/log

    [Install]
    WantedBy=multi-user.target

Enable and start the service:

.. sourcecode:: bash

    systemctl daemon-reload
    systemctl enable uts-server
    systemctl start  uts-server
    systemctl status uts-server

Running with Docker
-------------------

A minimal ``Dockerfile``:

.. sourcecode:: dockerfile

    FROM debian:bookworm-slim

    RUN apt-get update && apt-get install -y \
            uts-server \
        && rm -rf /var/lib/apt/lists/*

    COPY uts-server.cnf /etc/uts-server/uts-server.cnf
    COPY pki/            /etc/uts-server/pki/

    EXPOSE 318
    CMD ["uts-server", "-c", "/etc/uts-server/uts-server.cnf"]

Build and run:

.. sourcecode:: bash

    docker build -t uts-server .
    docker run -d \
        -p 318:318 \
        --name uts-server \
        uts-server

Access control
--------------

Restrict which IP ranges may submit timestamp requests using the
``access_control_allow_origin`` parameter:

.. sourcecode:: ini

    # Allow only the 192.168.0.0/16 subnet, deny everything else
    access_control_allow_origin = -0.0.0.0/0,+192.168.0.0/16

    # Allow a specific host plus a /24 subnet
    access_control_allow_origin = -0.0.0.0/0,+10.0.0.5,+10.0.1.0/24

Client usage examples
---------------------

Using ``openssl ts`` directly
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: bash

    # 1. Create a timestamp request for a file (SHA-256 hash)
    openssl ts -query \
        -data  /path/to/file.pdf \
        -sha256 \
        -cert \
        -out   /tmp/file.tsq

    # 2. Submit the request to uts-server
    curl -s \
        -H "Content-Type: application/timestamp-query" \
        --data-binary @/tmp/file.tsq \
        -o /tmp/file.tsr \
        http://tsa.example.com:318/

    # 3. Verify the timestamp response
    openssl ts -verify \
        -data    /path/to/file.pdf \
        -in      /tmp/file.tsr \
        -CAfile  /etc/uts-server/pki/tsaca.pem \
        -untrusted /etc/uts-server/pki/tsacert.pem

    # 4. Display the timestamp token details
    openssl ts -reply -in /tmp/file.tsr -text

Using the bundled helper script
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: bash

    # Basic usage: timestamp a file (output written to file.pdf.tsr)
    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/

    # Specify a custom output path and request the signer cert be embedded
    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/ \
        -o /path/to/file.tsr \
        -O "-cert"

    # Overwrite an existing .tsr and log to syslog
    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/ \
        -r -l

    # Use SHA-1 hashing (e.g. for compatibility with legacy systems)
    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/ \
        -O "-sha1"

    # Connect through an HTTPS endpoint, ignoring certificate errors (testing only)
    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u https://tsa.example.com:3161/ \
        -C "-k"

Using ``curl`` manually (without the helper)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: bash

    # Build request and send in one pipeline
    openssl ts -query -data README.rst -sha256 -out - | \
        curl -s \
            -H "Content-Type: application/timestamp-query" \
            --data-binary @- \
            -o README.rst.tsr \
            http://tsa.example.com:318/

Windows (signtool)
~~~~~~~~~~~~~~~~~~

Windows ``signtool`` can use an RFC 3161 TSA for countersigning Authenticode binaries:

.. sourcecode:: bat

    REM Sign and timestamp an executable
    signtool sign /fd sha256 /tr http://tsa.example.com:318/ /td sha256 myapp.exe

    REM Verify the timestamp
    signtool verify /pa /v myapp.exe

Java (BouncyCastle)
~~~~~~~~~~~~~~~~~~~

.. sourcecode:: java

    import org.bouncycastle.tsp.*;
    import java.net.*;
    import java.io.*;

    // Build request
    TimeStampRequestGenerator gen = new TimeStampRequestGenerator();
    gen.setCertReq(true);
    byte[] hash = MessageDigest.getInstance("SHA-256").digest(data);
    TimeStampRequest req = gen.generate(TSPAlgorithms.SHA256, hash);

    // Send request
    URL url = new URL("http://tsa.example.com:318/");
    HttpURLConnection conn = (HttpURLConnection) url.openConnection();
    conn.setDoOutput(true);
    conn.setRequestMethod("POST");
    conn.setRequestProperty("Content-Type", "application/timestamp-query");
    conn.getOutputStream().write(req.getEncoded());

    // Read response
    byte[] respBytes = conn.getInputStream().readAllBytes();
    TimeStampResponse resp = new TimeStampResponse(respBytes);
    resp.validate(req);

Python (using ``requests`` and ``asn1crypto``)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: python

    import hashlib, requests
    from asn1crypto import tsp, algos, core

    # Build a minimal TimeStampReq (DER-encoded)
    data = open("file.pdf", "rb").read()
    digest = hashlib.sha256(data).digest()

    msg_imprint = tsp.MessageImprint({
        'hash_algorithm': algos.DigestAlgorithm({'algorithm': 'sha256'}),
        'hashed_message': digest,
    })
    ts_req = tsp.TimeStampReq({
        'version':         1,
        'message_imprint': msg_imprint,
        'cert_req':        True,
    })

    # Submit to uts-server
    resp = requests.post(
        "http://tsa.example.com:318/",
        headers={"Content-Type": "application/timestamp-query"},
        data=ts_req.dump(),
    )
    resp.raise_for_status()

    # The response body is a DER-encoded TimeStampResp
    ts_resp = tsp.TimeStampResp.load(resp.content)
    print(ts_resp["status"]["status"].native)   # should be "granted"

Verifying a timestamp with OpenSSL
------------------------------------

.. sourcecode:: bash

    # Verify against the original file
    openssl ts -verify \
        -data   /path/to/original-file \
        -in     /path/to/file.tsr \
        -CAfile /path/to/tsaca.pem

    # Verify when the CA cert is in a directory (hashed with c_rehash)
    openssl ts -verify \
        -data    /path/to/original-file \
        -in      /path/to/file.tsr \
        -CApath  /etc/ssl/certs/

    # Inspect the timestamp token without verifying
    openssl ts -reply -in /path/to/file.tsr -text

    # Extract the raw CMS/PKCS#7 token for inspection
    openssl ts -reply -in /path/to/file.tsr -token_out -out /tmp/token.p7s
    openssl pkcs7 -inform DER -in /tmp/token.p7s -print_certs -noout

Log management
--------------

By default uts-server logs to **syslog**.  To additionally (or exclusively) log to stdout
set the following in ``[ main ]``:

.. sourcecode:: ini

    log_to_syslog = no
    log_to_stdout = yes
    log_level     = info

Available log levels (from most to least verbose): ``debug``, ``info``, ``notice``,
``warn``, ``err``, ``crit``, ``emerg``.

Each request generates a one-liner at ``info`` level:

.. sourcecode:: text

    LOG_INFO: Request[A1B2C3D4], remote_addr[10.0.0.5] ssl[0] uri[/] \
              http_resp_code[200] duration[4821 us] \
              user-agent[curl/8.5.0] content-type[application/timestamp-query]

The ``Request[...]`` field is a prefix derived from the timestamp serial number and
can be used to correlate debug lines with the request log entry.

Security response headers
-------------------------

uts-server automatically injects the following HTTP security headers into **every**
response, regardless of the endpoint or HTTP method:

+-----------------------------------+-------------------------------------------------+
| Header                            | Value / Purpose                                 |
+===================================+=================================================+
| ``X-Content-Type-Options``        | ``nosniff`` — prevents MIME-type sniffing       |
+-----------------------------------+-------------------------------------------------+
| ``X-Frame-Options``               | ``DENY`` — blocks clickjacking via iframe       |
+-----------------------------------+-------------------------------------------------+
| ``Referrer-Policy``               | ``no-referrer`` — suppresses Referer leakage    |
+-----------------------------------+-------------------------------------------------+
| ``Content-Security-Policy``       | ``default-src 'none'; style-src 'unsafe-inline``|
|                                   | `` '`` — allows only inline styles (needed for  |
|                                   | the built-in status page), blocks all external  |
|                                   | resources and scripts                           |
+-----------------------------------+-------------------------------------------------+
| ``Cache-Control``                 | ``no-store`` — prevents caching of timestamp    |
|                                   | tokens and certificate downloads                |
+-----------------------------------+-------------------------------------------------+

The following two headers are sent **only on HTTPS connections** (``is_ssl = 1``):

HTTP Strict Transport Security (HSTS)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: text

    Strict-Transport-Security: max-age=31536000; includeSubDomains

HSTS instructs browsers to always use HTTPS for the hostname (and all subdomains)
for the next year.  This mitigates protocol-downgrade and cookie-hijacking attacks.

.. note::

    HSTS only takes effect when the server terminates TLS directly
    (``ssl_certificate`` set in ``[ main ]``) or when a TLS-terminating reverse
    proxy forwards the header.  Browsers ignore HSTS on plain HTTP connections.

HTTP Public Key Pinning (HPKP)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: text

    Public-Key-Pins: pin-sha256="<base64-SPKI>"; max-age=2592000

HPKP pins the SHA-256 hash of the TSA signing certificate's
*SubjectPublicKeyInfo* (SPKI).  A browser that has seen this pin will reject any
future TLS certificate chain that does not include a matching key, even if it was
issued by a trusted CA — thereby mitigating attacks that use fraudulently issued
certificates.

The pin is computed automatically at startup from the ``signer_cert`` configured in
``[ tsa ]`` and is logged at ``NOTICE`` level so you can verify it:

.. sourcecode:: text

    LOG_NOTICE: HPKP pin (signer cert): LvpE2hxZwgsM4gjhEhaNTUdQxVmO2wdk6f6lWvSjAXw=

You can cross-check the pin with OpenSSL:

.. sourcecode:: bash

    openssl x509 -in /etc/uts-server/pki/tsacert.pem -pubkey -noout \
        | openssl pkey -pubin -outform DER \
        | openssl dgst -sha256 -binary \
        | base64

.. warning::

    HPKP is powerful but risky.  If you rotate the signing key **without** first
    advertising the new pin alongside the old one, clients that have cached the
    old pin will be locked out for ``max-age`` seconds.  Always:

    * Pre-pin the replacement key at least ``max-age`` seconds before the old
      certificate expires.
    * Keep an offline backup pin (generated from a backup key pair) and add it
      as a second ``pin-sha256`` value before the primary pin expires.
    * Start with a low ``max-age`` (e.g. 300 s) and only increase it once you
      have verified everything works correctly.

    HPKP (RFC 7469) has been removed from most browsers by default, but it
    remains supported in several enterprise and embedded HTTP clients that
    interact with TSAs.

Verifying headers with curl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: bash

    curl -Isv http://tsa.example.com:318/ 2>&1 | grep "< "
    # Expected (HTTP):
    # < X-Content-Type-Options: nosniff
    # < X-Frame-Options: DENY
    # < Referrer-Policy: no-referrer
    # < Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'
    # < Cache-Control: no-store

    curl -Isv https://tsa.example.com:3161/ 2>&1 | grep "< "
    # Expected (HTTPS — additional headers):
    # < Strict-Transport-Security: max-age=31536000; includeSubDomains
    # < Public-Key-Pins: pin-sha256="..."; max-age=2592000

Troubleshooting
---------------

Server fails to start with "cannot bind"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: text

    LOG_ERR: Failed to start uts-server: cannot bind to 0.0.0.0:318: 98

Either another process is already using the port, or the process does not have
permission to bind a privileged port.  Check with:

.. sourcecode:: bash

    ss -tlnp | grep 318
    # or
    lsof -i :318

For privileged ports, see the :ref:`pki-setup` section on ``setcap`` or ``run_as_user``.

Timestamp request rejected (HTTP 400)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Possible causes:

* The requested **digest algorithm** is not listed in the ``digests`` parameter of ``[ tsa ]``.
* The requested **policy OID** is not in ``default_policy`` or ``other_policies``.
* The request is malformed (wrong ``Content-Type`` header or truncated body).

Enable debug logging (``-D`` flag or ``log_level = debug``) to see the full
OpenSSL error chain:

.. sourcecode:: bash

    ./uts-server -c /etc/uts-server/uts-server.cnf -D

Certificate / signing errors
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* **"failed to initialize tsa context"** — Check that ``signer_cert``, ``signer_key`` and
  ``certs`` all point to readable files.
* **"error … in OpenSSL component ts"** — The TSA certificate may be missing the
  ``critical,timeStamping`` EKU.  Inspect with:

  .. sourcecode:: bash

      openssl x509 -in /etc/uts-server/pki/tsacert.pem -noout -purpose | grep "Time Stamp"
      # Expected: Time Stamp signing : Yes

