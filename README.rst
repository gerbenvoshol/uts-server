uts-server
==========

.. image:: https://github.com/kakwa/uts-server/blob/master/docs/assets/logo_64.png?raw=true

|

.. image:: https://travis-ci.org/kakwa/uts-server.svg?branch=master
    :target: https://travis-ci.org/kakwa/uts-server

.. image:: https://readthedocs.org/projects/uts-server/badge/?version=latest
    :target: http://uts-server.readthedocs.org/en/latest/?badge=latest
    :alt: Documentation Status

.. image:: https://jenkins.kakwalab.ovh/buildStatus/icon?job=kakwa/uts-server/master
    :target: https://jenkins.kakwalab.ovh/blue/organizations/jenkins/kakwa%2Futs-server/branches/
    :alt: Jenkins Status

Micro `RFC 3161 Time-Stamp <https://www.ietf.org/rfc/rfc3161.txt>`_ server written in C.

----

:Doc:    `Uts-Server documentation on ReadTheDoc <http://uts-server.readthedocs.org/en/latest/>`_
:Dev:    `Uts-Server source code on GitHub <https://github.com/kakwa/uts-server>`_
:License: MIT
:Author:  Pierre-Francois Carpentier - copyright © 2019

----

Demo
----

A demo is accessible here: https://uts-server.kakwalab.ovh/

License
-------

Released under the MIT Public License

What is RFC 3161?
-----------------

`RFC 3161 <https://www.ietf.org/rfc/rfc3161.txt>`_ defines the Internet X.509 Public Key Infrastructure
**Time-Stamp Protocol (TSP)**.  A time-stamp is a cryptographic proof that a specific piece of data
existed in its current form at a particular point in time — issued by a trusted third-party called a
**Time-Stamp Authority (TSA)**.

How it works
~~~~~~~~~~~~

1. The client computes a hash (e.g. SHA-256) of the data it wants to time-stamp.
2. The client wraps the hash in a ``TimeStampReq`` ASN.1 structure and sends it over HTTP/HTTPS to the TSA.
3. The TSA retrieves the current time, embeds it together with the hash in a ``TSTInfo`` structure, and
   signs the whole thing with its private key to produce a ``TimeStampToken`` (a CMS ``SignedData``).
4. The TSA returns the ``TimeStampResp`` to the client.
5. Anyone who holds the TSA's CA certificate chain can verify the token with ``openssl ts -verify``.

Crucially, only the **hash** of the data is sent to the TSA — the original data never leaves the client.

Use cases
~~~~~~~~~

* **Log rotation** — time-stamp log files at the moment they are rotated so tampering can be detected later.
* **Document signing** — attach an unforgeable timestamp to legally-significant documents (contracts, invoices).
* **Software builds** — prove that a binary was produced before a certificate was revoked.
* **Code signing** — Windows ``signtool`` and most code-signing workflows support RFC 3161 countersignatures.
* **Audit trails** — add trusted timestamps to database records or API event logs.
* **Regulatory compliance** — many eIDAS and industry regulations require trusted timestamps on electronic signatures.

Architecture overview
~~~~~~~~~~~~~~~~~~~~~

uts-server is a lightweight, multi-threaded HTTP/HTTPS server built on top of two battle-tested libraries:

* **OpenSSL** — handles all cryptographic operations (hashing, signing, certificate management, TLS).
* **civetweb** — provides the embedded HTTP/HTTPS server layer.

Each incoming ``POST /`` request with ``Content-Type: application/timestamp-query`` is dispatched to one
worker thread from a configurable pool.  The thread picks a pre-initialised ``TS_RESP_CTX`` from a
thread-local pool, generates the response, and returns it to the client.  GET requests to ``/`` return
a human-readable landing page with usage instructions.

.. code-block:: text

    Client                         uts-server
    ------                         ----------
    hash(data) ─── TSQ (HTTP POST) ──► rfc3161_handler()
                                           │
                                       get_ctxw()          ← thread-safe context pool
                                           │
                                       TS_RESP_create_response()   ← OpenSSL
                                           │
               TSR (HTTP 200) ◄─── i2d_TS_RESP_fp()

Security notes
~~~~~~~~~~~~~~

* Use a **dedicated CA** for the TSA — do not reuse an existing TLS or code-signing CA.
* The TSA signing certificate **must** have ``extendedKeyUsage = critical,timeStamping`` and **only** that EKU.
* Protect the TSA private key with filesystem permissions (``chmod 400``) and, ideally, an HSM or
  a hardware token.
* Deploy behind a reverse proxy (nginx, HAProxy) for production TLS termination, or use the built-in
  ``ssl_certificate`` option.
* Restrict network access with ``access_control_allow_origin`` or firewall rules so that only authorised
  clients can submit timestamp requests.
* Rotate the TSA signing certificate periodically; keep old certificates so previously issued tokens
  remain verifiable.

Quick (and dirty) Testing
-------------------------

The steps below use the bundled test PKI and are intended for local experimentation only.
For a production setup see the `full documentation <http://uts-server.readthedocs.org/en/latest/>`_.

.. sourcecode:: bash

    # Build with civetweb bundled (development/testing only — not for production).
    $ cmake . -DBUNDLE_CIVETWEB=ON
    $ make

    # Generate a self-signed TSA CA and a TSA signing certificate.
    $ ./tests/cfg/pki/create_tsa_certs

    # Start the server in foreground debug mode (logs go to stdout).
    $ ./uts-server -c tests/cfg/uts-server.cnf -D

    # --- In a second terminal ---

    # Time-stamp a file using the helper script.
    $ ./goodies/timestamp-file.sh -i README.rst -u http://localhost:2020 -r -O "-cert"

    # Verify the time-stamp against the original file and the test CA.
    $ openssl ts -verify \
          -in  README.rst.tsr \
          -data README.rst \
          -CAfile ./tests/cfg/pki/tsaca.pem \
          -untrusted ./tests/cfg/pki/tsa_cert1.pem

    # Inspect the time-stamp token content.
    $ openssl ts -reply -in README.rst.tsr -text

    # Alternatively, use plain curl + openssl without the helper script.
    $ openssl ts -query -data README.rst -sha256 -cert -out /tmp/req.tsq
    $ curl -s http://localhost:2020 \
          -H "Content-Type: application/timestamp-query" \
          --data-binary @/tmp/req.tsq \
          -o /tmp/reply.tsr
    $ openssl ts -reply -in /tmp/reply.tsr -text

Powered by
----------

.. image:: https://raw.githubusercontent.com/openssl/web/master/img/openssl-64.png
    :target: https://www.openssl.org/

.. image:: https://github.com/civetweb/civetweb/blob/658c8d48b3bcdb34338dae1b83167a8d7836e356/resources/civetweb_32x32@2.png?raw=true
    :target: https://github.com/civetweb/civetweb
