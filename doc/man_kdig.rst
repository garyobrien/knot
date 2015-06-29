.. highlight:: console

kdig – Advanced DNS lookup utility
==================================

Synopsis
--------

:program:`kdig` [*common-settings*] [*query* [*settings*]]...

:program:`kdig` **-h**

Description
-----------

This utility sends one or more DNS queries to a nameserver. Each query can have
individual *settings*, or it can be specified globally via *common-settings*,
which must precede *query* specification.

Parameters
..........

*query*
  *name* | **-q** *name* | **-x** *address* | **-G** *tapfile*

*common-settings*, *settings*
  [*class*] [*type*] [**@**\ *server*]... [*options*]

*name*
  Is a domain name that is to be looked up.

*server*
  Is a domain name or an IPv4 or IPv6 address of the nameserver to send a query
  to. An additional port can be specified using address:port ([address]:port
  for IPv6 address), address@port, or address#port notation. If no server is
  specified, the servers from :file:`/etc/resolv.conf` are used.

If no arguments are provided, :program:`kdig` sends NS query for the root
zone.

Options
.......

**-4**
  Use the IPv4 protocol only.

**-6**
  Use the IPv6 protocol only.

**-b** *address*
  Set the source IP address of the query to *address*. The address must be a
  valid address for local interface or :: or 0.0.0.0. An optional port
  can be specified in the same format as the *server* value.

**-c** *class*
  Set the query class (e.g. CH, CLASS4). An explicit variant of *class*
  specification. The default class is IN.

**-d**
  Enable debug messages.

**-h**, **--help**
  Print help and usage.

**-k** *keyfile*
  Use the TSIG key stored in a file *keyfile* to authenticate the request. The
  file must contain the key in the same format as accepted by the
  **-y** option.

**-p** *port*
  Set the nameserver port number or service name to send a query to. The default
  port is 53.

**-q** *name*
  Set the query name. An explicit variant of *name* specification.

**-t** *type*
  Set the query type (e.g. NS, IXFR=12345, TYPE65535, NOTIFY). An explicit 
  variant of *type* specification. The default type is A. IXFR type requires 
  SOA serial parameter. NOTIFY type without SOA serial parameter causes pure 
  NOTIFY message without any SOA hint.

**-v**, **--version**
  Print the program version.

**-x** *address*
  Send a reverse (PTR) query for IPv4 or IPv6 *address*. The correct name, class
  and type is set automatically.

**-y** [*alg*:]\ *name*:*key*
  Use the TSIG key named *name* to authenticate the request. The *alg*
  part specifies the algorithm (the default is hmac-md5) and *key* specifies
  the shared secret encoded in Base64.

**-E** *tapfile*
  Export a dnstap trace of the query and response messages received to the
  file *tapfile*.

**-G** *tapfile*
  Generate message output from a previously saved dnstap file *tapfile*.

**+**\ [\ **no**\ ]\ **multiline**
  Wrap long records to more lines and improve human readability.

**+**\ [\ **no**\ ]\ **short**
  Show record data only.

**+**\ [\ **no**\ ]\ **aaflag**
  Set the AA flag.

**+**\ [\ **no**\ ]\ **tcflag**
  Set the TC flag.

**+**\ [\ **no**\ ]\ **rdflag**
  Set the RD flag.

**+**\ [\ **no**\ ]\ **recurse**
  Same as **+**\ [\ **no**\ ]\ **rdflag**

**+**\ [\ **no**\ ]\ **raflag**
  Set the RA flag.

**+**\ [\ **no**\ ]\ **zflag**
  Set the zero flag bit.

**+**\ [\ **no**\ ]\ **adflag**
  Set the AD flag.

**+**\ [\ **no**\ ]\ **cdflag**
  Set the CD flag.

**+**\ [\ **no**\ ]\ **dnssec**
  Set the DO flag.

**+**\ [\ **no**\ ]\ **all**
  Show all packet sections.

**+**\ [\ **no**\ ]\ **qr**
  Show the query packet.

**+**\ [\ **no**\ ]\ **header**
  Show the packet header.

**+**\ [\ **no**\ ]\ **opt**
  Show the EDNS pseudosection.

**+**\ [\ **no**\ ]\ **question**
  Show the question section.

**+**\ [\ **no**\ ]\ **answer**
  Show the answer section.

**+**\ [\ **no**\ ]\ **authority**
  Show the authority section.

**+**\ [\ **no**\ ]\ **additional**
  Show the additional section.

**+**\ [\ **no**\ ]\ **tsig**
  Show the TSIG pseudosection.

**+**\ [\ **no**\ ]\ **stats**
  Show trailing packet statistics.

**+**\ [\ **no**\ ]\ **class**
  Show the DNS class.

**+**\ [\ **no**\ ]\ **ttl**
  Show the TTL value.

**+**\ [\ **no**\ ]\ **tcp**
  Use the TCP protocol (default is UDP for standard query and TCP for AXFR/IXFR).

**+**\ [\ **no**\ ]\ **fail**
  Stop querying next nameserver if SERVFAIL response is received.

**+**\ [\ **no**\ ]\ **ignore**
  Don't use TCP automatically if a truncated reply is received.

**+**\ [\ **no**\ ]\ **nsid**
  Request the nameserver identifier (NSID).

**+**\ [\ **no**\ ]\ **edns**\ =\ *N*
  Use EDNS version (default is 0).

**+noidn**
  Disable the IDN transformation to ASCII and vice versa. IDNA2003 support depends
  on libidn availability during project building!

**+generic**
  Use the generic representation format when printing resource record types
  and data.

**+client**\ =\ *SUBN*
  Set the EDNS client subnet SUBN=IP/prefix.

**+time**\ =\ *T*
  Set the wait-for-reply interval in seconds (default is 5 seconds). This timeout
  applies to each query attempt.

**+retry**\ =\ *N*
  Set the number (>=0) of UDP retries (default is 2). This doesn't apply to
  AXFR/IXFR.

**+bufsize**\ =\ *B*
  Set the EDNS buffer size in bytes (default is 512 bytes).

Notes
-----

Options **-k** and **-y** can not be used simultaneously.

Missing features with regard to ISC dig:

  Options **-f** and **-m** and query options:
  **+split**\ =\ *W*,
  **+tries**\ =\ *T*,
  **+ndots**\ =\ *D*,
  **+domain**\ =\ *somename*,
  **+trusted-key**\ =\ *####*,
  **+**\ [\ **no**\ ]\ **vc**,
  **+**\ [\ **no**\ ]\ **search**,
  **+**\ [\ **no**\ ]\ **showsearch**,
  **+**\ [\ **no**\ ]\ **defname**,
  **+**\ [\ **no**\ ]\ **aaonly**,
  **+**\ [\ **no**\ ]\ **cmd**,
  **+**\ [\ **no**\ ]\ **identify**,
  **+**\ [\ **no**\ ]\ **comments**,
  **+**\ [\ **no**\ ]\ **rrcomments**,
  **+**\ [\ **no**\ ]\ **onesoa**,
  **+**\ [\ **no**\ ]\ **besteffort**,
  **+**\ [\ **no**\ ]\ **sigchase**,
  **+**\ [\ **no**\ ]\ **topdown**,
  **+**\ [\ **no**\ ]\ **nssearch**, and
  **+**\ [\ **no**\ ]\ **trace**.

  Per-user file configuration via :file:`~/.digrc`.

Examples
--------

1. Get A records for example.com::

     $ kdig example.com A

2. Perform AXFR for zone example.com from the server 192.0.2.1::

     $ kdig example.com -t AXFR @192.0.2.1

3. Get A records for example.com from 192.0.2.1 and reverse lookup for address
   2001:DB8::1 from 192.0.2.2. Both using the TCP protocol::

     $ kdig +tcp example.com -t A @192.0.2.1 -x 2001:DB8::1 @192.0.2.2

Files
-----

:file:`/etc/resolv.conf`

See Also
--------

:manpage:`khost(1)`, :manpage:`knsupdate(1)`.