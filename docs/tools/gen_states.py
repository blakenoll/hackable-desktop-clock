#!/usr/bin/env python3
"""Regenerates the clock-state diagrams in docs/states/*.svg.

Run from anywhere: python3 docs/tools/gen_states.py
Coordinates are in the 240x240 display space of src/ui/, scaled up when drawn. Nothing is read
from the firmware, so update the numbers and colours here when the UI changes.
"""
import os, math
OUT=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','states')
base,bright,dim='#56606D','#8E9EB3','#2A3138'
T=12
def mix(a,b,o):
    ca=[int(a[i:i+2],16) for i in (1,3,5)];cb=[int(b[i:i+2],16) for i in (1,3,5)]
    return '#%02x%02x%02x'%tuple(round((x*o+y*(255-o))/255) for x,y in zip(ca,cb))
def tailc(b):
    f=255*(T-b+1)//(T+1);return mix(bright,base,f*f//255)
def ring(cx,cy,s,mode,sec=0,accent='#FF3030'):
    if mode=='none':return ''
    o=''
    for i in range(60):
        major=i%5==0;w=3 if major else 2;col=dim
        if mode=='sec':
            if i==sec:col=accent
            elif i<sec:b=sec-i;col=tailc(b) if b<=T else base
            else:
                wr=sec+60-i
                if wr<=T:col=tailc(wr)
        elif mode=='comet':
            b=sec-i
            if b==0:col=accent
            elif 0<b<=T:col=tailc(b)
        a=math.radians(i*6-90);r1=117*s;r2=r1-(12 if major else 7)*s
        o+=f'<line x1="{cx+r1*math.cos(a):.1f}" y1="{cy+r1*math.sin(a):.1f}" x2="{cx+r2*math.cos(a):.1f}" y2="{cy+r2*math.sin(a):.1f}" stroke="{col}" stroke-width="{w*s:.2f}" stroke-linecap="round"/>'
    return o
def bezel(cx,cy,s): return f'<circle cx="{cx}" cy="{cy}" r="{125*s:.1f}" fill="#8a929c"/><circle cx="{cx}" cy="{cy}" r="{121.5*s:.1f}" fill="#000"/>'
def txt(cx,cy,s,dy,f,t,fill,anchor='middle',dx=0,w=500,extra=''):
    return f'<text x="{cx+dx*s:.1f}" y="{cy+s*(dy+.36*f):.1f}" text-anchor="{anchor}" font-size="{f*s:.1f}" font-weight="{w}" fill="{fill}" {extra}>{t}</text>'
def datetime_(cx,cy,s,night=False,time='10:47',ampm='AM',accent='#FF4D4D'):
    dayc,datec,tc,ac=('#D01818','#901010','#D01818','#901010') if night else (accent,'#A0A0A0','#fff','#A0A0A0')
    o=f'<text x="{cx}" y="{cy+s*(-62+.36*20):.1f}" text-anchor="middle" font-size="{20*s:.1f}" font-weight="500"><tspan fill="{dayc}">Tue</tspan><tspan dx="{6*s}" fill="{datec}">Nov 23</tspan></text>'
    o+=txt(cx,cy,s,0,56,time,tc,w=500,extra=f'letter-spacing="{-1*s}"')
    o+=txt(cx,cy,s,0,14,ampm,ac,'start',dx=80,w=500).replace(f'y="{cy+s*(0+.36*14):.1f}"',f'y="{cy+s*20:.1f}"')
    return o
def icon_partly(x,y,s,col):  # x,y = top-left of a 30x30 display box
    return f'<g fill="{col}" stroke="{col}" stroke-linecap="round" transform="translate({x},{y}) scale({s/2})"><circle cx="22" cy="24" r="12" stroke="none"/><path d="M22 4V0M22 44V48M2 24H-2M42 24H46M8 10L5 7M36 10L39 7" stroke-width="3.5"/><ellipse cx="34" cy="44" rx="22" ry="12" stroke="none"/><circle cx="24" cy="38" r="10" stroke="none"/><circle cx="40" cy="34" r="12" stroke="none"/></g>'
def icon_rain(x,y,s,col):
    return f'<g fill="{col}" stroke="{col}" stroke-linecap="round" transform="translate({x},{y}) scale({s/2})"><ellipse cx="30" cy="22" rx="24" ry="13" stroke="none"/><circle cx="20" cy="15" r="11" stroke="none"/><circle cx="38" cy="12" r="13" stroke="none"/><path d="M14 42L10 54M28 42L24 54M42 42L38 54" stroke-width="4"/></g>'
def weather(cx,cy,s,icon,col,temp,rng=None,night=False,tcol='#D0D8E0'):
    o=''
    # row centre +62; day layout: icon, temp, bar
    if night:
        x0=cx-35*s; tc='#C01515'; col='#C01515'
    else: x0=cx-55*s; tc=tcol
    o+=icon(x0,cy+s*(62-15),s,col)
    o+=f'<text x="{x0+38*s:.1f}" y="{cy+s*(62+.36*20):.1f}" font-size="{20*s}" font-weight="500" fill="{tc}">{temp}</text>'
    if rng and not night:
        lo,hi,cur=rng;bx=cx+27*s;by=cy+s*(62-17)
        gid=f'g{int(cx)}{int(cy)}'
        o+=f'<linearGradient id="{gid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FF8A65"/><stop offset="1" stop-color="#6FB6FF"/></linearGradient>'
        o+=f'<rect x="{bx:.1f}" y="{by:.1f}" width="{4*s}" height="{34*s}" rx="{2*s}" fill="url(#{gid})"/>'
        fr=(cur-lo)/(hi-lo);dy=by+s*((1-fr)*24+5)
        o+=f'<circle cx="{bx+2*s:.1f}" cy="{dy:.1f}" r="{5*s}" fill="#000"/><circle cx="{bx+2*s:.1f}" cy="{dy:.1f}" r="{3*s}" fill="#fff"/>'
        o+=f'<text x="{bx+9*s:.1f}" y="{by+s*10:.1f}" font-size="{14*s}" font-weight="500" fill="#FF8A65">{hi}°</text><text x="{bx+9*s:.1f}" y="{by+s*34:.1f}" font-size="{14*s}" font-weight="500" fill="#6FB6FF">{lo}°</text>'
    return o
def page(name,title,lines,body,code,note=None,proposed=False):
    cx,cy=300,290
    side=''
    y=140
    if proposed:
        side+=f'<rect x="590" y="{y-20}" width="96" height="24" rx="12" fill="#fff4d6" stroke="#e9a100"/><text x="638" y="{y-3}" text-anchor="middle" font-size="12" font-weight="700" fill="#8a5a00">PROPOSED</text>'
        y+=34
    for i,l in enumerate(lines):
        side+=f'<text x="590" y="{y+i*20}" font-size="14" fill="{"#1f2328" if i==0 else "#57606a"}" font-weight="{700 if i==0 else 400}">{l}</text>'
    side+=f'<text x="590" y="{y+len(lines)*20+14}" font-size="12" font-family="ui-monospace,Menlo,Consolas,monospace" fill="#0891b2">{code}</text>'
    svg=f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 900 580" width="900" height="580" font-family="Manrope,-apple-system,'Segoe UI',Helvetica,Arial,sans-serif">
  <title>{title}</title>
  <rect width="900" height="580" rx="12" fill="#ffffff" stroke="#d0d7de"/>
  <text x="450" y="30" text-anchor="middle" font-size="16" font-weight="600" fill="#1f2328">{title}</text>
  <g transform="translate(0,10)">{bezel(cx,cy,2)}{body(cx,cy,2)}</g>
  {side}
  <text x="450" y="566" text-anchor="middle" font-size="11" fill="#57606a">{note or 'Drawn at 2x from the 240×240 display layout in src/ui/; animated states show one frame.'}</text>
</svg>
'''
    open(f'{OUT}/{name}.svg','w').write(svg)
S=2
# 1 boot splash
page('boot-splash','State: boot splash',['Boot splash','A comet runs once around the seconds ring','(about 1 second) before anything else shows.'],
 lambda cx,cy,s:ring(cx,cy,s,'comet',sec=22),'ui_boot_splash()')
# 2 connecting
def wifi(cx,cy,s):
    c=cy+18*s;o=ring(cx,cy,s,'none')+ring(cx,cy,s,'sec',sec=-1) if False else ring(cx,cy,s,'comet',sec=-1)
    for i,(d,on) in enumerate([(38,1),(62,1),(86,0)]):
        r=(d/2-3.5)*s;col='#fff' if on else '#303030'
        a1,a2=math.radians(225),math.radians(315)
        o+=f'<path d="M{cx+r*math.cos(a1):.1f} {c+r*math.sin(a1):.1f} A{r:.1f} {r:.1f} 0 0 1 {cx+r*math.cos(a2):.1f} {c+r*math.sin(a2):.1f}" fill="none" stroke="{col}" stroke-width="{7*s}" stroke-linecap="round"/>'
    o+=f'<circle cx="{cx}" cy="{c}" r="{5*s}" fill="#fff"/>'
    return o
page('connecting-wifi','State: connecting to WiFi',['Connecting to WiFi','Arcs light up in sequence while the clock joins','the saved network. After 20 s without success (or a','3 s BOOT press) it drops into setup mode.'],wifi,'ui_set_connecting(true)')
# 3 syncing
def sync(cx,cy,s):
    c=cy-14*s;o=ring(cx,cy,s,'comet',sec=-1)
    o+=f'<circle cx="{cx}" cy="{c}" r="{39.5*s}" fill="none" stroke="#fff" stroke-width="{5*s}"/>'
    m=math.radians(120);h=math.radians(10)
    o+=f'<line x1="{cx}" y1="{c}" x2="{cx+20*s*math.sin(h):.1f}" y2="{c-20*s*math.cos(h):.1f}" stroke="#FF4D4D" stroke-width="{5*s}" stroke-linecap="round"/>'
    o+=f'<line x1="{cx}" y1="{c}" x2="{cx+30*s*math.sin(m):.1f}" y2="{c-30*s*math.cos(m):.1f}" stroke="#fff" stroke-width="{4*s}" stroke-linecap="round"/><circle cx="{cx}" cy="{c}" r="{5*s}" fill="#fff"/>'
    return o+txt(cx,cy,s,52,20,'Synchronizing','#fff')
page('synchronizing','State: synchronizing time',['Synchronizing','WiFi is up; waiting for the first NTP time.','The hands spin (minute hand 12x the hour hand).'],sync,'ui_set_syncing(true)')
# 4 setup
def setup(cx,cy,s):
    g='#808080'
    return txt(cx,cy,s,-54,14,'Join this WiFi',g)+txt(cx,cy,s,-28,20,'clk-setup-A1B2','#fff')+txt(cx,cy,s,8,14,'Password',g)+txt(cx,cy,s,34,20,'48213906','#fff')+txt(cx,cy,s,72,14,'then open 192.168.4.1',g)
page('setup-mode','State: WiFi setup screen',['Setup mode','Shown on first boot, after 20 s without WiFi, or after','holding BOOT for 3 s. Join the clock\'s own network,','then open the address to enter WiFi, timezone, ZIP','and display options. SSID suffix and password vary.'],setup,'ui_show_setup(ssid, pass, ip)','The SSID suffix is the last two MAC bytes; the password is random per session. Values shown are examples.')
# 5 scanning
def scan(cx,cy,s):
    g='#2EE59D';o=ring(cx,cy,s,'comet',sec=-1)
    for ph in (.15,.48,.82):
        d=(ph*96*2+4)/2*s
        o+=f'<circle cx="{cx}" cy="{cy}" r="{d:.1f}" fill="none" stroke="{g}" stroke-width="{2*s}" opacity="{(1-ph)*.78:.2f}"/>'
    for k,(op,w) in enumerate([(1,3),(.5,2),(.2,2)]):
        a=math.radians(40-14*k)
        o+=f'<line x1="{cx}" y1="{cy}" x2="{cx+96*s*math.sin(a):.1f}" y2="{cy-96*s*math.cos(a):.1f}" stroke="{g}" stroke-width="{w*s}" stroke-linecap="round" opacity="{op}"/>'
    return o+f'<circle cx="{cx}" cy="{cy}" r="{4*s}" fill="{g}"/>'+txt(cx,cy,s,70,20,'Scanning',g)
page('scanning','State: scanning for WiFi networks',['Scanning','Sonar pings and a rotating sweep while the setup','page looks for nearby networks (before the setup','access point starts).'],scan,'ui_set_scanning(true)')
# 6 weather loading
def loading(cx,cy,s):
    o=ring(cx,cy,s,'sec',sec=47)+datetime_(cx,cy,s)
    g='#4A4F57';y=cy+62*s
    o+=f'<circle cx="{cx-62*s}" cy="{y}" r="{12*s}" fill="{g}"/><rect x="{cx-40*s}" y="{y-8*s}" width="{56*s}" height="{16*s}" rx="{8*s}" fill="{g}"/>'
    bx=cx+30*s
    o+=f'<rect x="{bx}" y="{y-17*s}" width="{4*s}" height="{34*s}" rx="{2*s}" fill="{g}"/><rect x="{bx+9*s}" y="{y-18*s}" width="{20*s}" height="{8*s}" rx="{4*s}" fill="{g}"/><rect x="{bx+9*s}" y="{y+10*s}" width="{20*s}" height="{8*s}" rx="{4*s}" fill="{g}"/>'
    return o
page('weather-loading','State: weather loading',['Weather loading','Right after boot, until the first weather fetch','returns, a pulsing skeleton stands in for the icon,','temperature and range bar. Time and date work as normal.'],loading,'ui_set_weather_loading(true)')
# 7 weather error
def werr(msg):
    def f(cx,cy,s):
        return ring(cx,cy,s,'sec',sec=47)+datetime_(cx,cy,s)+txt(cx,cy,s,62,20,msg,'#D0D8E0')
    return f
page('weather-zip-not-found','State: ZIP code not found',['ZIP not found','The saved ZIP could not be geocoded. The weather','row shows this text instead of an icon. Fix the ZIP','on the setup page (hold BOOT 3 s).'],werr('ZIP not found'),'ui_set_weather(nullptr, 0, "ZIP not found", ...)')
page('weather-unavailable','State: weather unavailable',['No weather','The fetch failed (no internet, API error). The clock','keeps working and retries every 60 s. Stale data is','kept on screen if there is any.'],werr('No weather'),'ui_set_weather(nullptr, 0, "No weather", ...)')
# 8 rain
page('rain','State: rain',['Rain','Weather codes 61-65 use the rain icon (blue #4DA3FF);','showers, drizzle, sleet, snow, fog and thunder each have','their own icon and colour. The dot on the bar marks the','current temperature between today\'s low and high.'],
 lambda cx,cy,s:ring(cx,cy,s,'sec',sec=47)+datetime_(cx,cy,s)+weather(cx,cy,s,icon_rain,'#4DA3FF','58°',(52,61,58)),'weather_icon(code 61..65)')
# 9 night
page('night-mode','State: night mode',['Night mode','After sunset (from the weather feed\'s is_day) everything','turns dim red; the seconds ring and range bar fade to','black so nothing moves. It cross-fades over ~45 s.'],
 lambda cx,cy,s:datetime_(cx,cy,s,night=True)+weather(cx,cy,s,icon_partly,'#C01515','61°',night=True),'ui_set_night(true)')
# 10 chance of rain (proposed)
def chance(cx,cy,s):
    o=ring(cx,cy,s,'sec',sec=47)+datetime_(cx,cy,s)+weather(cx,cy,s,icon_rain,'#4DA3FF','58°',(52,61,58))
    c='#6FB6FF';y=cy+92*s
    o+=f'<path d="M{cx-23*s} {y-6*s} q{-4*s} {6*s} 0 {9*s} q{4*s} {-3*s} 0 {-9*s}z" fill="{c}" transform="translate(0,0)"/>'
    o+=txt(cx,cy,s,92,14,'40%',c,'start',dx=-16)
    return o+f'<rect x="{cx-45*s}" y="{y-11*s}" width="{90*s}" height="{22*s}" rx="{6*s}" fill="none" stroke="#36D6F0" stroke-width="3" stroke-dasharray="8 6"/>'
page('chance-of-rain','Mockup: chance of rain',['Chance of rain','Same slot as the feels-like line, centre + 92 px:','a small drop and the percentage in the low-temp blue','(#6FB6FF, Manrope 14). Not in the firmware yet.'],chance,'precipitation_probability (Open-Meteo)',proposed=True,note='Mockup only: the firmware does not fetch or show rain probability yet.')
# 11 accents gallery
def gallery():
    names=[('Red','#FF4D4D','#FF3030'),('Amber','#FFB020','#FFA010'),('Green','#4CE08A','#30E070'),('Cyan','#36D6F0','#20D0F0'),('Pink','#FF6EC7','#FF4DB8'),('White','#FFFFFF','#FFFFFF')]
    s=.62;o=''
    for k,(n,t,tk) in enumerate(names):
        cx=150+k%3*300;cy=190+k//3*270
        o+=bezel(cx,cy,s)+ring(cx,cy,s,'sec',sec=17+k*8,accent=tk)
        o+=datetime_(cx,cy,s,accent=t,ampm='AM')
        o+=f'<text x="{cx}" y="{cy+135}" text-anchor="middle" font-size="14" font-weight="600" fill="#1f2328">{n}</text>'
    svg=f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 900 580" width="900" height="580" font-family="Manrope,-apple-system,'Segoe UI',Helvetica,Arial,sans-serif">
  <title>Accent colours</title>
  <rect width="900" height="580" rx="12" fill="#ffffff" stroke="#d0d7de"/>
  <text x="450" y="30" text-anchor="middle" font-size="16" font-weight="600" fill="#1f2328">Accent colours (setup page option)</text>
  {o}
  <text x="450" y="566" text-anchor="middle" font-size="11" fill="#57606a">The accent colours the weekday label and the current-second tick. Night mode stays red whatever the accent.</text>
</svg>
'''
    open(f'{OUT}/accent-colors.svg','w').write(svg)
gallery()
G='#8A949E'
def detail(cx,cy,s,t,night=False,hl=False):
    o=txt(cx,cy,s,90,14,t,'#901010' if night else G)
    if hl:o+=f'<rect x="{cx-48*s}" y="{cy+s*80}" width="{96*s}" height="{20*s}" rx="{6*s}" fill="none" stroke="#36D6F0" stroke-width="3" stroke-dasharray="8 6"/>'
    return o
def drops(cx,cy,s,kind,col):
    x0=cx-55*s;top=cy+s*(62-15);w=30;h=32;o=''
    for k,(xp,v) in enumerate(zip((30,46,62,78),(.15,.55,.35,.8))):
        x=x0+s*w*xp/100;y=top+s*(h*.62+10*v+ (3 if kind==1 else 0))
        op=.8*(1-v) if v>.2 else v*4*.8
        if kind==1:o+=f'<rect x="{x-1*s:.1f}" y="{y:.1f}" width="{2*s}" height="{5*s}" rx="{s}" fill="{col}" opacity="{max(op,.25):.2f}"/>'
        else:o+=f'<circle cx="{x+2*s*math.sin(v*6.28):.1f}" cy="{y:.1f}" r="{1.5*s}" fill="{col}" opacity="{max(op,.25):.2f}"/>'
    return o
def icon_snow(x,y,s,col):
    return f'<g fill="{col}" stroke="{col}" stroke-linecap="round" transform="translate({x},{y}) scale({s/2})"><ellipse cx="30" cy="22" rx="24" ry="13" stroke="none"/><circle cx="20" cy="15" r="11" stroke="none"/><circle cx="38" cy="12" r="13" stroke="none"/><path d="M16 44v8M12 48h8M30 44v8M26 48h8M44 44v8M40 48h8" stroke-width="3"/></g>'
base_day=lambda cx,cy,s:ring(cx,cy,s,'sec',sec=47)+datetime_(cx,cy,s)
RAIN='#4DA3FF'
# feels-like (real)
page('feels-like','State: feels-like line',['Feels-like line','Small line under the weather row (centre + 90 px,','Manrope 14, grey #8A949E; dim red #901010 at night).','Shown only when no rain is likely and feels-like differs','from the temperature by 5°F (3°C) or more.'],
 lambda cx,cy,s:base_day(cx,cy,s)+weather(cx,cy,s,icon_partly,'#FFD66B','72°',(61,78,72))+detail(cx,cy,s,'Feels 66°',hl=True),'ui_set_weather_detail("Feels 66°")')
# rain chance (real)
page('chance-of-rain','State: chance of rain',['Rain chance','Same detail line shows "NN% rain" when the highest','precipitation probability over the next 6 hours is 30%','or more. It takes priority over the feels-like text.'],
 lambda cx,cy,s:base_day(cx,cy,s)+weather(cx,cy,s,icon_partly,'#FFD66B','68°',(58,72,68))+detail(cx,cy,s,'40% rain',hl=True),'ui_set_weather_detail("40% rain")')
# rain animation
page('rain','State: rain',['Rain','Rain codes (61-65, 80-82) use the blue rain icon. For a','few seconds after the weather updates, four drops drift','down under the icon (day only; night stays still).','The detail line shows the rain chance.'],
 lambda cx,cy,s:base_day(cx,cy,s)+weather(cx,cy,s,icon_rain,RAIN,'58°',(52,61,58))+drops(cx,cy,s,1,RAIN)+detail(cx,cy,s,'80% rain'),'weather_fall(code) == 1')
page('snow','State: snow',['Snow','Snow codes use the pale snow icon; four flakes sway','down under it for a few seconds (day only).'],
 lambda cx,cy,s:base_day(cx,cy,s)+weather(cx,cy,s,icon_snow,'#E6F2FF','28°',(22,31,28))+drops(cx,cy,s,2,'#E6F2FF')+detail(cx,cy,s,'Feels 19°'),'weather_fall(code) == 2')
# stale
def stale(cx,cy,s):
    return base_day(cx,cy,s)+'<g opacity=".35">'+weather(cx,cy,s,icon_partly,'#FFD66B','72°',(61,78,72))+detail(cx,cy,s,'Feels 66°')+'</g>'
page('stale-weather','State: stale weather',['Stale weather','If the last successful fetch is over 30 minutes old','(for example the internet is down) the weather row and','detail line fade to about 35% so old data is clearly','not current. They return to full brightness on refresh.'],stale,'ui_set_weather_stale(true)')
# offline
def offline(cx,cy,s):
    o=base_day(cx,cy,s)+weather(cx,cy,s,icon_partly,'#FFD66B','72°',(61,78,72))
    bx=cx-10*s;by=cy+(-91-8)*s;px=lambda x,y:(bx+x*s,by+y*s)
    c=px(10,13)
    for r in (4.5,9):
        a1,a2=math.radians(225),math.radians(315)
        p1=(c[0]+r*s*math.cos(a1),c[1]+r*s*math.sin(a1));p2=(c[0]+r*s*math.cos(a2),c[1]+r*s*math.sin(a2))
        o+=f'<path d="M{p1[0]:.1f} {p1[1]:.1f} A{r*s:.1f} {r*s:.1f} 0 0 1 {p2[0]:.1f} {p2[1]:.1f}" fill="none" stroke="{G}" stroke-width="{2*s}" stroke-linecap="round"/>'
    o+=f'<circle cx="{c[0]:.1f}" cy="{c[1]:.1f}" r="{1.6*s}" fill="{G}"/>'
    p1,p2=px(2,1),px(18,15)
    o+=f'<line x1="{p1[0]:.1f}" y1="{p1[1]:.1f}" x2="{p2[0]:.1f}" y2="{p2[1]:.1f}" stroke="{G}" stroke-width="{2*s}" stroke-linecap="round"/>'
    return o
page('offline','State: WiFi offline',['Offline marker','After 10 s without WiFi a small crossed-out WiFi glyph','fades in above the date (grey #8A949E, dim red at night).','Brief blips never show it. The clock keeps time and the','weather dims once it goes stale.'],offline,'ui_set_offline(true)')
# hourly chime
def chime(cx,cy,s):
    step=30;o=ring(cx,cy,s,'sec',sec=3)
    # overlay comet ticks
    for i in range(max(0,step-T),step+1):
        b=step-i;col='#FF3030' if b==0 else tailc(b);major=i%5==0;w=3 if major else 2
        a=math.radians(i*6-90);r1=117*s;r2=r1-(12 if major else 7)*s
        o+=f'<line x1="{cx+r1*math.cos(a):.1f}" y1="{cy+r1*math.sin(a):.1f}" x2="{cx+r2*math.cos(a):.1f}" y2="{cy+r2*math.sin(a):.1f}" stroke="{col}" stroke-width="{w*s}" stroke-linecap="round"/>'
    return o+datetime_(cx,cy,s,time='11:00')+weather(cx,cy,s,icon_partly,'#FFD66B','72°',(61,78,72))
page('hourly-chime','State: hourly chime',['Hourly chime','At the top of every hour a comet laps the seconds ring','once (about 1 s), then the ring returns to the real','second. Day only: the quiet night face never moves.'],chime,'startChime()')
