Some Goodies
============

Time-Stamp script combining curl and openssl
--------------------------------------------

The ``goodies/timestamp-file.sh`` script is a convenient shell client that wraps
``openssl ts`` and ``curl`` to submit RFC 3161 timestamp requests to uts-server.

Usage
~~~~~

.. sourcecode:: text

    timestamp-file.sh -i <input file> -u <ts server url>
                      [-o <output ts file>] [-O <openssl options>]
                      [-C <curl options>] [-r] [-l]

    Flags:
      -i <file>    Input file to timestamp (mandatory)
      -u <url>     Timestamp server URL (mandatory)
      -o <file>    Output .tsr file (default: <input file>.tsr)
      -O <opts>    Extra options passed to ``openssl ts -query``
      -C <opts>    Extra options passed to ``curl``
      -r           Overwrite an existing output file
      -l           Enable logging to syslog
      -h           Show help

Examples
~~~~~~~~

**Basic usage** — timestamp a file and write the response to ``file.pdf.tsr``:

.. sourcecode:: bash

    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/

**Embed the signer certificate** in the token (``-cert`` flag passed to openssl):

.. sourcecode:: bash

    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/ \
        -O "-cert"

**Specify a custom output path** and overwrite if it already exists:

.. sourcecode:: bash

    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/ \
        -o /var/timestamps/file.tsr \
        -r

**Request SHA-1 hashing** (e.g. for compatibility with legacy systems):

.. sourcecode:: bash

    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/ \
        -O "-sha1"

**HTTPS endpoint, ignoring certificate errors** (testing/development only):

.. sourcecode:: bash

    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u https://tsa.example.com:3161/ \
        -C "-k"

**Log to syslog** as well as the terminal:

.. sourcecode:: bash

    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/ \
        -l

**Combine multiple flags** — embed cert, overwrite, log to syslog:

.. sourcecode:: bash

    ./goodies/timestamp-file.sh \
        -i /path/to/file.pdf \
        -u http://tsa.example.com:318/ \
        -O "-cert -sha256" \
        -r -l

Script source
~~~~~~~~~~~~~

.. literalinclude:: ../goodies/timestamp-file.sh
    :language: bash

