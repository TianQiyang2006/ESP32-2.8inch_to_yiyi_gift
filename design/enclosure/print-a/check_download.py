from urllib.request import urlopen
from urllib.parse import urljoin
import html.parser
import sys
import time

class Links(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
    def handle_starttag(self, tag, attrs):
        if tag == 'a':
            self.links += [v for k,v in attrs if k == 'href']

t=time.time()
parser=Links()
parser.feed(urlopen(sys.argv[1]+'/simple/cadquery-ocp/',timeout=12).read().decode())
u=next(x for x in parser.links if '7.8.1.1.post1-cp313-cp313-win_amd64.whl' in x)
r=urlopen(urljoin(sys.argv[1]+'/simple/cadquery-ocp/',u),timeout=12)
a=r.read(1048576)
print(len(a),round(time.time()-t,2),flush=True)
