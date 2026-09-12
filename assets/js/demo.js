(function(){
   var root=document.getElementById('demo');
   if(!root)return;
   var navs=root.querySelectorAll('.dm-navi'),panels=root.querySelectorAll('.dm-panel');
   navs.forEach(function(n){n.addEventListener('click',function(){
    var i=+n.dataset.tab;
    navs.forEach(function(x){x.classList.remove('dm-on')});
    panels.forEach(function(p){p.classList.remove('dm-on')});
    n.classList.add('dm-on');panels[i].classList.add('dm-on');
   });});

   root.querySelectorAll('.dm-tog').forEach(function(t){t.addEventListener('click',function(){t.classList.toggle('dm-on')})});
   var chips=root.querySelectorAll('.dm-chip');
   chips.forEach(function(c){c.addEventListener('click',function(){chips.forEach(function(x){x.classList.remove('dm-on')});c.classList.add('dm-on')})});

   // Network test tab: fake a realistic-looking measurement on click
   var netBtn=root.querySelector('#dm-net-start'),netPing=root.querySelector('#dm-net-ping'),
       netDown=root.querySelector('#dm-net-down'),netUp=root.querySelector('#dm-net-up'),netRunning=false;
   if(netBtn)netBtn.addEventListener('click',function(){
    if(netRunning)return;netRunning=true;
    netBtn.textContent='Тестирование…';
    setTimeout(function(){
     if(netPing)netPing.textContent=(38+Math.floor(Math.random()*10))+' мс';
     if(netDown)netDown.textContent=(180+Math.floor(Math.random()*60));
     if(netUp)netUp.textContent=(35+Math.floor(Math.random()*15));
     netBtn.innerHTML='<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="6 3 20 12 6 21 6 3"/></svg>Начать тест';
     netRunning=false;
    },1100);
   });

   // Profile tab: a fresh-looking random key each page load (not the same string for every visitor),
   // same alphabet as the real KeyGenerator — excludes 0/O/1/I to avoid look-alike characters.
   var keyEl=root.querySelector('#dm-key-val');
   if(keyEl){
    var ALPHA='23456789ABCDEFGHJKLMNPQRSTUVWXYZ';
    function randKey(){
     var groups=[];
     for(var g=0;g<4;g++){
      var s='';
      for(var c=0;c<4;c++) s+=ALPHA[Math.floor(Math.random()*ALPHA.length)];
      groups.push(s);
     }
     return groups.join('-');
    }
    keyEl.textContent=randKey();
   }

   var copyBtn=root.querySelector('.dm-key-val')?.nextElementSibling;
   if(copyBtn)copyBtn.addEventListener('click',async function(){
    try { await navigator.clipboard.writeText(keyEl.textContent); copyBtn.textContent='Скопировано'; }
    catch { copyBtn.textContent='Не удалось скопировать'; }
    setTimeout(function(){copyBtn.textContent='Копировать';},1400);
   });

   // Быстрая очистка → gauge jumps into the healthy zone
   var garc=root.querySelector('#dm-garc'),gnum=root.querySelector('#dm-gnum'),glbl=root.querySelector('#dm-glbl'),cleaned=false;
   function clean(){
    if(cleaned)return;cleaned=true;
    if(garc){garc.setAttribute('stroke','var(--green)');garc.setAttribute('stroke-dashoffset','176');}
    if(gnum)gnum.textContent='46%';
    if(glbl){glbl.textContent='Отлично';glbl.classList.add('dm-good');}
    var f=root.querySelector('#dm-freed'),d=root.querySelector('#dm-dpc');
    if(f)f.innerHTML='3.1<span>ГБ</span>';
    if(d)d.innerHTML='39<span>мкс</span>';
   }
   [root.querySelector('#dm-clean'),root.querySelector('.dm-qa .dm-btn')].forEach(function(b){if(b)b.addEventListener('click',clean)});

   var srch=root.querySelector('#dm-psearch'),rows=root.querySelectorAll('#dm-ptable tbody tr');
   if(srch)srch.addEventListener('input',function(){
    var q=srch.value.toLowerCase();
    rows.forEach(function(r){r.style.display=r.cells[0].textContent.toLowerCase().indexOf(q)>-1?'':'none'});
   });

   var exlist=root.querySelector('#dm-exuser'),exin=root.querySelector('#dm-exinput'),exadd=root.querySelector('#dm-exadd');
   function bindRm(b){b.addEventListener('click',function(){b.closest('.dm-exitem').remove()})}
   root.querySelectorAll('.dm-exrm').forEach(bindRm);
   function addEx(){
    var v=(exin.value||'').trim();if(!v)return;
    var d=document.createElement('div');d.className='dm-exitem';
    d.innerHTML='<svg class="dm-star" viewBox="0 0 24 24"><path d="M12 2l3 6.5 7 .8-5 4.8 1.3 7L12 17.8 5.4 21l1.3-7-5-4.8 7-.8z"/></svg><b></b><button class="dm-btn dm-btn-xs dm-btn-g dm-exrm">Убрать</button>';
    d.querySelector('b').textContent=v;
    bindRm(d.querySelector('.dm-exrm'));exlist.appendChild(d);exin.value='';
   }
   if(exadd)exadd.addEventListener('click',addEx);
   if(exin)exin.addEventListener('keydown',function(e){if(e.key==='Enter')addEx()});

   var b1=root.querySelector('#dm-runopt'),b2=root.querySelector('#dm-runopt2'),
       prog=root.querySelector('#dm-prog'),term=root.querySelector('#dm-term'),
       freed=root.querySelector('#dm-freed'),dpc=root.querySelector('#dm-dpc'),running=false,done=false;
   var steps=['Создание точки восстановления… ОК','Отключение телеметрии (DiagTrack)… ОК','Настройка сетевых задержек (TCP Nodelay)… ОК','Сжатие неактивного ОЗУ… ОК','Готово: 200+ твиков'];
   var reduce=window.matchMedia('(prefers-reduced-motion: reduce)').matches;
   function finish(){if(freed)freed.innerHTML='2.4<span>ГБ</span>';if(dpc)dpc.innerHTML='38<span>мкс</span>';running=false;done=true}
   function run(){
    if(running)return;running=true;
    term.classList.add('dm-show');term.textContent='';prog.style.width='0%';
    if(reduce){term.textContent=steps.join('\n');prog.style.width='100%';finish();return;}
    var i=0;
    (function next(){
     if(i>=steps.length){finish();return;}
     term.textContent+=(i?'\n':'')+steps[i];
     prog.style.width=Math.round((i+1)/steps.length*100)+'%';
     i++;setTimeout(next,650);
    })();
   }
   if(b1)b1.addEventListener('click',run);
   if(b2)b2.addEventListener('click',run);

   if(dpc)setInterval(function(){
    if(running||done||cleaned)return;
    dpc.innerHTML=(140+Math.floor(Math.random()*10))+'<span>мкс</span>';
   },2200);
  })();


  (function(){
    var win = document.querySelector('#demo .dm-win');
    if(!win) return;
    var profs = win.querySelectorAll('.dm-prof');
    if(!profs || !profs.length) return;
    var NAMES = ['standard', 'gaming', 'maximum'];
    var PALETTE = [
      { a:'34,224,107',  b:'18,184,147'  },
      { a:'34,211,238',  b:'59,130,246'  },
      { a:'255,106,26',  b:'255,61,87'   }
    ];
    function tint(i){
      var p = PALETTE[i] || PALETTE[1];
      win.style.setProperty('--accent-rgb', p.a);
      win.style.setProperty('--accent-2-rgb', p.b);
      win.style.setProperty('--profile-rgb', p.a);
      win.style.setProperty('--grad-brand','linear-gradient(120deg,rgb('+p.a+'),rgb('+p.b+'))');
      for(var j=0;j<profs.length;j++){ profs[j].classList.toggle('dm-on', j===i); }
    }
    // Clicking a profile icon here drives the hero's own switcher — one source of truth,
    // so the pill up top, its own app-window and this demo can never disagree again.
    for(var k=0;k<profs.length;k++){
      (function(idx){
        profs[idx].addEventListener('click', function(){
          if (window.pulseSelectProfile) window.pulseSelectProfile(NAMES[idx]);
          else tint(idx);
        });
      })(k);
    }
    window.addEventListener('pulse-profile-change', function(e){
      tint(e.detail.idx);
    });
    // Initial paint: match whatever profile is already active (hero default or saved state),
    // not a hardcoded guess.
    var current = document.documentElement.getAttribute('data-profile') || 'gaming';
    tint(NAMES.indexOf(current) >= 0 ? NAMES.indexOf(current) : 1);
  })();
