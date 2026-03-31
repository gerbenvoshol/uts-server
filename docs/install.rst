Dependencies
============

Runtime dependencies
--------------------

List of dependencies uts-server relies on at runtime:

* `OpenSSL <https://github.com/openssl/openssl>`_ (1.0.x, 1.1.x, 3.x) or
  `LibreSSL <https://www.libressl.org/>`_
* `civetweb <https://github.com/civetweb/civetweb>`_
* On systems without GNU libc (FreeBSD, OpenBSD):
  `argp-standalone <https://www.lysator.liu.se/~nisse/misc/argp-standalone-1.3.tar.gz>`_

Build dependencies
------------------

* `CMake <https://cmake.org/>`_ ≥ 2.6
* `gcc <https://gcc.gnu.org/>`_ or `clang <https://clang.llvm.org/>`_
* OpenSSL development headers (``libssl-dev`` / ``openssl-devel``)
* civetweb development headers (``libcivetweb-dev`` or built from source)

Compilation
===========

uts-server is compiled using cmake:

.. sourcecode:: bash

    # Standard build when civetweb is already installed on the system
    $ cmake .
    $ make

    # Bundle civetweb from upstream GitHub (development/testing only)
    $ cmake . -DBUNDLE_CIVETWEB=ON
    $ make

    # Debug build (no optimisation, debug symbols)
    $ cmake . -DDEBUG=ON
    $ make

    # Static binary
    # In some cases you may need additional link flags for dl, gcc_s, or pthread:
    $ cmake . -DSTATIC=ON  # add -DLINK_DL=ON -DLINK_GCC_S=ON -DLINK_PTHREAD=ON if needed
    $ make

.. warning::

    The ``BUNDLE_CIVETWEB`` option exists **only** for development and testing.

    Please compile civetweb externally for production binaries.  Using this option
    in production is a bad idea because:

    * it downloads from the internet at build time (bad for reproducible builds),
    * pulling ``master`` may break your build at any time, and
    * a production build should pin all dependencies to a known-good version.

Verifying the build
-------------------

After compiling, run the built-in integration tests to confirm everything is working:

.. sourcecode:: bash

    # Generate test PKI (one-time setup)
    $ ./tests/cfg/pki/create_tsa_certs

    # Run the integration test suite
    $ ./tests/external_test.sh

A successful run prints no error lines and exits with code 0.

CMake build options reference
------------------------------

+------------------------+---------+----------------------------------------------------------+
| Option                 | Default | Description                                              |
+========================+=========+==========================================================+
| ``BUNDLE_CIVETWEB``    | OFF     | Clone and build civetweb from GitHub (dev/test only)     |
+------------------------+---------+----------------------------------------------------------+
| ``DEBUG``              | OFF     | Compile with ``-O0 -g`` (debug symbols, no optimisation) |
+------------------------+---------+----------------------------------------------------------+
| ``STATIC``             | OFF     | Produce a statically-linked binary                       |
+------------------------+---------+----------------------------------------------------------+
| ``LINK_DL``            | OFF     | Link against ``libdl`` (needed for some static builds)   |
+------------------------+---------+----------------------------------------------------------+
| ``LINK_GCC_S``         | OFF     | Link against ``libgcc_s`` (needed for some static builds)|
+------------------------+---------+----------------------------------------------------------+
| ``LINK_PTHREAD``       | OFF     | Link against ``libpthread``                              |
+------------------------+---------+----------------------------------------------------------+
| ``OPENSSL_API_1_1``    | auto    | Override OpenSSL API version detection                   |
+------------------------+---------+----------------------------------------------------------+

OS-specific installation notes
================================

Debian / Ubuntu
---------------

.. sourcecode:: bash

    # Install build dependencies
    $ sudo apt-get update
    $ sudo apt-get install -y \
          cmake gcc libssl-dev libcivetweb-dev

    # Build and install
    $ cmake .
    $ make
    $ sudo make install

    # (Alternative) install civetweb from source if the packaged version is too old
    $ git clone https://github.com/civetweb/civetweb.git
    $ cd civetweb && cmake . -DCIVETWEB_ENABLE_SSL=ON && make && sudo make install
    $ cd ..
    $ cmake . && make

CentOS / RHEL / Fedora
-----------------------

.. sourcecode:: bash

    # Install build dependencies (RHEL/CentOS 7+)
    $ sudo yum install -y cmake gcc gcc-c++ openssl-devel

    # Fedora
    $ sudo dnf install -y cmake gcc gcc-c++ openssl-devel

    # civetweb must be built from source on most RHEL/CentOS versions
    $ git clone https://github.com/civetweb/civetweb.git
    $ cd civetweb && cmake . -DCIVETWEB_ENABLE_SSL=ON && make && sudo make install
    $ cd ..
    $ cmake . && make

FreeBSD
-------

.. sourcecode:: bash

    # Install build dependencies
    $ pkg install argp-standalone cmake

    # civetweb from ports
    $ pkg install civetweb

    # Build
    $ cmake . && make

OpenBSD
-------

.. sourcecode:: bash

    # Install build dependencies
    $ pkg_add gcc g++ argp-standalone cmake

    # For the test scripts
    $ pkg_add python curl

    # OpenBSD ships an old gcc — use egcc/ec++ instead
    $ export CC=/usr/local/bin/egcc
    $ export CXX=/usr/local/bin/ec++

    # Build
    $ cmake . -DBUNDLE_CIVETWEB=ON && make

macOS (Homebrew)
----------------

.. sourcecode:: bash

    $ brew install cmake openssl civetweb

    # Point CMake at the Homebrew OpenSSL installation
    $ cmake . -DOPENSSL_ROOT_DIR=$(brew --prefix openssl)
    $ make

