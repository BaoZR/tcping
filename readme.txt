这个程序和原来的程序相比，增加了定时运行功能和统计jittor的功能

Installation:
------------
make install


Usage:
-----
tcping hostname
(uses port 80) or
tcping -p port hostname

ping once:
tcping -p port -c 1 hostname

tcping returns:
0 on success
2 if the host or service could not be resolved
127 on other errors

examples:
tcping -p 8080 127.0.0.1

Please support my opensource development: http://www.vanheusden.com/wishlist.php
