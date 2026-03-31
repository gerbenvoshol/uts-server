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

Optional chroot / filesystem isolation
---------------------------------------

uts-server supports an optional chroot jail via the ``-r CHROOT_DIR``
(``--chroot-dir``) command-line flag.  When this flag is given the process
calls ``chroot(2)`` and ``chdir("/")`` *before opening or parsing* any
configuration file or network socket, so the entire server lifetime (config
parsing, PKI loading, TLS handshakes, serving) operates within the jail.

Why chroot here works cleanly
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Most servers struggle with chroot because they keep reading files at
runtime (e.g. a serial-number file updated on every request).
uts-server has no such requirement:

* **Serial numbers** are generated with ``RAND_bytes()`` — no file I/O.
* **Signing cert and private key** are loaded into OpenSSL memory once at
  startup by ``create_tsctx()``; after that, signing never touches the
  filesystem.
* **``/ca.pem`` and ``/tsa_cert.pem`` download endpoints** re-read their
  files per request, so those PEM files must be inside the jail.
* **TLS certificate / key** (optional) is opened by civetweb during
  ``mg_start()``, which runs after the chroot, so it must also be inside
  the jail.

.. note::

    ``chroot(2)`` requires root or the ``CAP_SYS_CHROOT`` Linux capability.
    The typical pattern is: start as root, pass ``-r CHROOT_DIR``, and also
    set ``run_as_user`` in the configuration so that civetweb drops
    privileges inside the jail after opening the listening socket.

Setting up the jail directory
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The jail must contain the configuration file and all files it references.
A minimal layout for a plain-HTTP deployment:

.. sourcecode:: text

    /srv/uts-server/                  ← CHROOT_DIR (-r argument)
    ├── uts-server.cnf                ← config file (-c /uts-server.cnf)
    └── pki/
        ├── tsacert.pem
        ├── cacert.pem
        └── private/
            └── tsakey.pem           ← chmod 400, owned by uts-server

For a TLS deployment add the combined server key+cert PEM:

.. sourcecode:: text

    /srv/uts-server/
    └── server.pem                    ← ssl_certificate = /server.pem

Create the structure and copy files:

.. sourcecode:: bash

    JAIL=/srv/uts-server
    sudo mkdir -p $JAIL/pki/private

    # Copy PKI files from their current location into the jail
    sudo cp /etc/uts-server/pki/tsacert.pem  $JAIL/pki/
    sudo cp /etc/uts-server/pki/cacert.pem   $JAIL/pki/
    sudo cp /etc/uts-server/pki/private/tsakey.pem $JAIL/pki/private/

    # Lock down the private key
    sudo chown root:uts-server $JAIL/pki/private/tsakey.pem
    sudo chmod 400 $JAIL/pki/private/tsakey.pem

    # Copy / create the configuration file inside the jail.
    # All [ tsa ] paths use the jail-relative root ("/pki/…" not "/etc/…").
    sudo cp /etc/uts-server/uts-server.cnf $JAIL/uts-server.cnf

The configuration file inside the jail must reference paths as they appear
**inside the jail** (i.e. starting from ``/``):

.. sourcecode:: ini

    [ tsa ]
    dir         = /pki
    signer_cert = $dir/tsacert.pem
    certs       = $dir/cacert.pem
    signer_key  = $dir/private/tsakey.pem

Launch with the ``-r`` flag, pointing ``-c`` at the jail-relative path:

.. sourcecode:: bash

    sudo uts-server -r /srv/uts-server -c /uts-server.cnf -D

For daemon mode with a PID file (the path is also jail-relative):

.. sourcecode:: bash

    sudo mkdir -p /srv/uts-server/run
    sudo uts-server \
        -r /srv/uts-server \
        -c /uts-server.cnf \
        -d \
        -p /run/uts-server.pid

The PID file will be created at ``/srv/uts-server/run/uts-server.pid`` on
the real filesystem (visible as ``/run/uts-server.pid`` inside the jail).

Syslog inside a chroot
~~~~~~~~~~~~~~~~~~~~~~~

uts-server calls ``openlog()`` with ``LOG_NDELAY`` **before** performing
the chroot so that the Unix socket connection to ``/dev/log`` is established
on the real filesystem.

For **foreground mode** (``-D``, no ``-d``) this is sufficient: the socket
fd remains open throughout the process lifetime.

For **daemon mode** (``-d``) the double-fork closes all file descriptors,
including the syslog socket.  uts-server then calls ``openlog()`` again
after the fork — but inside the jail ``/dev/log`` is not accessible, so
syslog silently fails.  To keep syslog working in daemon-inside-chroot
mode, either:

1. **Bind-mount** the real ``/dev/log`` into the jail at startup
   (requires systemd or a manual mount in the init script):

   .. sourcecode:: bash

       # Create the mount point
       sudo mkdir -p /srv/uts-server/dev
       # Bind-mount /dev/log
       sudo mount --bind /dev/log /srv/uts-server/dev/log

2. **Use stdout logging** and let systemd/journald capture it:

   .. sourcecode:: ini

       [ main ]
       log_to_syslog = no
       log_to_stdout = yes

``run_as_user`` inside a chroot
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

civetweb's ``run_as_user`` option calls ``getpwnam(3)`` to look up the
username, which requires ``/etc/passwd`` and ``/etc/group`` to be readable
inside the jail.  Create minimal copies:

.. sourcecode:: bash

    JAIL=/srv/uts-server
    sudo mkdir -p $JAIL/etc

    # Extract just the uts-server entry
    sudo grep '^uts-server:' /etc/passwd > $JAIL/etc/passwd
    sudo grep '^uts-server:' /etc/group  > $JAIL/etc/group
    # Also include the 'nobody' / 'nogroup' entries if needed
    sudo grep '^nobody:'    /etc/passwd >> $JAIL/etc/passwd
    sudo grep '^nogroup:'   /etc/group  >> $JAIL/etc/group
    sudo chmod 644 $JAIL/etc/passwd $JAIL/etc/group

Alternatively, drop privileges **before** the chroot: remove
``run_as_user`` from the config and use systemd's ``User=`` directive (or
``start-stop-daemon -c user:group``) which drops privileges before
``exec``-ing the binary.  In that case the binary only needs
``CAP_NET_BIND_SERVICE`` rather than ``CAP_SYS_CHROOT`` (which requires
root).  See the systemd service example below.

Systemd service with chroot
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Systemd provides its own ``RootDirectory=`` directive which is equivalent
to ``chroot`` and integrates with the unit file lifecycle.  For most
deployments this is simpler than passing ``-r`` manually:

.. sourcecode:: ini

    [Service]
    Type=forking
    User=uts-server
    Group=uts-server
    RootDirectory=/srv/uts-server
    ExecStart=/usr/sbin/uts-server \
        -c /uts-server.cnf \
        -d \
        -p /run/uts-server.pid
    PIDFile=/srv/uts-server/run/uts-server.pid
    RuntimeDirectory=uts-server
    RuntimeDirectoryMode=0755

With ``RootDirectory=`` systemd sets up ``/proc``, ``/dev/log``, and other
pseudo-filesystems inside the jail automatically, so syslog works without
extra configuration.

Use ``-r CHROOT_DIR`` when you need the chroot to be applied by
uts-server itself (e.g. in a SysV init or Docker environment without
systemd's ``RootDirectory=``).

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
|                                   | ``'`` — allows only inline styles (needed for   |
|                                   | the built-in status page), blocks all external  |
|                                   | resources and scripts                           |
+-----------------------------------+-------------------------------------------------+
| ``Cache-Control``                 | ``no-store`` — prevents caching of timestamp    |
|                                   | tokens and certificate downloads                |
+-----------------------------------+-------------------------------------------------+

The following header is sent **only on HTTPS connections** (``is_ssl = 1``):

HTTP Strict Transport Security (HSTS)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. sourcecode:: text

    Strict-Transport-Security: max-age=31536000; includeSubDomains

HSTS instructs clients to always use HTTPS for the hostname (and all subdomains)
for the next year.  This mitigates protocol-downgrade and cookie-hijacking attacks.

.. note::

    HSTS only takes effect when the server terminates TLS directly
    (``ssl_certificate`` set in ``[ main ]``) or when a TLS-terminating reverse
    proxy forwards the header.  HSTS is ignored on plain HTTP connections.

Why HPKP is not implemented
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

HTTP Public Key Pinning (HPKP, RFC 7469) was explicitly considered and
**intentionally left out** for the following reasons:

1. **It has no audience in the TSA context.**  HPKP was removed from Chrome 72
   (January 2019) and is disabled by default in Firefox.  The clients that actually
   talk to a TSA — ``openssl ts``, ``curl``, Java BouncyCastle, .NET
   ``System.Security.Cryptography``, Windows ``signtool`` — have never implemented
   it.  A header that nothing enforces provides zero security benefit.

2. **The risk–benefit ratio is inverted.**  A misconfigured or forgotten pin with a
   long ``max-age`` value can permanently lock out every client that *does* honour it,
   with no recovery path short of waiting for the pin to expire.  RFC 7469 itself
   lists "hostile pinning" as a primary threat vector.

3. **Better alternatives exist for the same threat model** (a rogue CA issuing a
   fraudulent certificate for your TSA hostname):

   * **CAA DNS records** — restrict which certificate authorities are permitted to
     issue certificates for your domain:

     .. sourcecode:: text

         tsa.example.com. IN CAA 0 issue "letsencrypt.org"
         tsa.example.com. IN CAA 0 issuewild ";"

   * **Certificate Transparency (CT) monitoring** — enrol in a CT log monitoring
     service (e.g. `crt.sh <https://crt.sh>`_) to receive alerts when any CA
     issues a certificate for your hostname.

   * **Explicit trust anchors in clients** — TSA clients should be configured with
     the specific CA certificate (``-CAfile /etc/uts-server/pki/tsaca.pem``) rather
     than relying on the system trust store.  This is a far stronger guarantee than
     HPKP because it works independently of browser support.

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

