import urllib.request, ssl, json
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
text = urllib.request.urlopen('https://raw.githubusercontent.com/Langegen/switch-games/refs/heads/main/switch_games.json', context=ctx).read().decode('utf-8', errors='ignore')
data = json.loads(text)
res = []
for d in data:
    if '[v' in d.get('title', ''):
        res.append((d.get('title'), d.get('title_id')))
print(res[:20])
