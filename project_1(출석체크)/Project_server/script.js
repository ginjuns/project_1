let currentCalendarDate = new Date();
let allAttendedTimes = {}; 
let currentAdminViewDate = "";
let currentCourseId = null;
let adminRefreshInterval = null;
let courseScheduleData = { weekly: [], cancel: [], special: [] };

window.onload = function() {
    hideAllSections();
    const savedId = sessionStorage.getItem("student_id");
    const savedName = sessionStorage.getItem("username");
    const savedIsAdmin = sessionStorage.getItem("is_admin");
    if (savedId && savedName) showDashboard(savedName, savedIsAdmin);
    else { showLogin(); }
    try{ populateMonthYearPicker(); }catch(e){}
};

function hideAllSections() {
    ['auth-container', 'login-section', 'register-section', 'dashboard-section', 'main-section', 'admin-section', 
     'calendar-modal', 'detail-modal', 'modal-student-stats', 'modal-course', 'modal-course-code', 'modal-add-schedule', 
     'modal-cancel', 'modal-special'].forEach(id => {
        const el = document.getElementById(id); if(el) el.classList.add('hidden');
    });
}

/* Auth & Nav */
function showLogin() { 
    hideAllSections(); 
    document.getElementById('auth-container').classList.remove('hidden'); 
    document.getElementById('login-section').classList.remove('hidden'); 
}
function showRegister() { 
    document.getElementById('auth-container').classList.remove('hidden'); 
    document.getElementById('login-section').classList.add('hidden'); 
    document.getElementById('register-section').classList.remove('hidden'); 
}
function showDashboard(name, isAdmin) {
    hideAllSections(); 
    document.getElementById('dashboard-section').classList.remove('hidden');
    document.getElementById('dash-welcome').innerText = name + (isAdmin==="1"?" 교수님":"님");
    document.getElementById(isAdmin==="1"?'admin-actions':'student-actions').classList.remove('hidden');
    document.getElementById(isAdmin==="1"?'student-actions':'admin-actions').classList.add('hidden');
    loadCourses();
}
function goBackToDashboard() { 
    if(adminRefreshInterval) clearInterval(adminRefreshInterval); 
    currentCourseId=null; 
    showDashboard(sessionStorage.getItem("username"), sessionStorage.getItem("is_admin")); 
}
function togglePw(id, icon) { 
    const inp=document.getElementById(id); 
    if(inp.type==="password"){inp.type="text";icon.innerText="🙈";} else{inp.type="password";icon.innerText="👁️";} 
}
function toggleAdminInput() { 
    const c=document.getElementById('admin-check'); 
    const i=document.getElementById('reg-admin-key'); 
    i.disabled=!c.checked; if(!c.checked)i.value=""; 
}
function processRegister() {
    const id=document.getElementById('reg-id').value, nm=document.getElementById('reg-name').value, pw=document.getElementById('reg-pw').value, k=document.getElementById('reg-admin-key').value;
    if(!id||!nm||!pw) return alert("입력 필요");
    fetch('/register',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`student_number=${id}&username=${nm}&pw=${pw}&admin_key=${k}`})
    .then(r=>{if(r.ok){alert("가입 완료");showLogin();}else alert("실패");});
}
function processLogin() {
    const id=document.getElementById('login-id').value, pw=document.getElementById('login-pw').value;
    fetch('/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`student_number=${id}&pw=${pw}`})
    .then(r=>{if(r.ok)return r.text(); throw new Error();}).then(d=>{
        const [n,a]=d.split(','); 
        sessionStorage.setItem("student_id",id); sessionStorage.setItem("username",n); sessionStorage.setItem("is_admin",a); 
        showDashboard(n,a);
    }).catch(()=>alert("로그인 실패"));
}
function processLogout(){ 
    if(adminRefreshInterval) clearInterval(adminRefreshInterval); 
    sessionStorage.clear(); location.reload(); 
}

/* Courses */
function createCourse(){
    const n=document.getElementById('new-course-name').value; 
    const pid=sessionStorage.getItem("student_id");
    if(!n) return alert("수업 이름을 입력해주세요.");
    
    fetch('/create-course',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`prof_id=${pid}&course_name=${n}`})
    .then(r=>r.text()).then(c=>{
        document.getElementById('new-course-name').value = "";
        showCourseCodeModal(c);
        loadCourses(); 
        closeModal('modal-course');
    })
    .catch(() => alert("수업 생성 실패"));
}
function joinCourse(){
    const c=document.getElementById('join-code').value, sid=sessionStorage.getItem("student_id");
    if(!c) return alert("코드 입력");
    fetch('/join-course',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`student_number=${sid}&code=${c}`})
    .then(r=>{if(r.ok){alert("가입 완료"); document.getElementById('join-code').value=""; loadCourses();}else alert("코드를 확인하세요.");});
}
function loadCourses(){
    const sid=sessionStorage.getItem("student_id"), adm=sessionStorage.getItem("is_admin");
    fetch('/get-my-courses',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`student_number=${sid}&is_admin=${adm}`})
    .then(r=>r.text()).then(d=>{
        const l=document.getElementById('my-course-list'); l.innerHTML="";
        if(!d){l.innerHTML="<p>수업 없음</p>";return;}
        
        d.split('|').filter(x=>x).forEach(r=>{
            const [id, n, c, prof, schedRaw] = r.split(',');
            const schedDisplay = formatScheduleString(schedRaw);
            const d=document.createElement('div'); d.className="course-card";
            d.innerHTML=`
                <div style="flex:1;">
                    <h4 style="margin-bottom:4px;">${n}</h4>
                    <div class="course-meta">
                        <span class="meta-item"> ${prof} 교수님</span>
                        <span class="meta-item" style="color:#2980b9;">${schedDisplay}</span>
                    </div>
                    <p class="course-code-text">Code: ${c}</p>
                </div>
                <div class="badge" style="background:#333">입장 ></div>`;
            d.onclick=()=>enterCourse(id,n,c,adm); 
            l.appendChild(d);
        });
    });
}
function formatScheduleString(raw) {
    if (!raw) return "⏳ 시간 미정";
    const parts = raw.split('&');
    let result = [];
    parts.forEach(p => {
        const [day, start, end] = p.split('@');
        result.push(`${getDayName(parseInt(day))} ${start}~${end}`);
    });
    return "📅 " + result.join(", ");
}
function enterCourse(cid,cname,ccode,adm){
    currentCourseId=cid; hideAllSections();
    if(adm==="1"){
        document.getElementById('admin-section').classList.remove('hidden');
        document.getElementById('class-title-adm').innerHTML = `${cname} <button class="btn-danger small-btn" style="margin-left:10px;" onclick="deleteCourse(${cid}, '${cname}')">수업 삭제</button>`;
        document.getElementById('class-code-display').innerText=ccode;
        const now=new Date(); 
        currentAdminViewDate=`${now.getFullYear()}-${String(now.getMonth()+1).padStart(2,'0')}-${String(now.getDate()).padStart(2,'0')}`;
        switchAdminView('dashboard');
    } else {
        document.getElementById('main-section').classList.remove('hidden');
        document.getElementById('class-title-stu').innerText=cname;
    }
}
function deleteCourse(courseId, courseName) {
    if(!confirm(`⚠️ 경고: [${courseName}] 수업을 삭제하면 모든 데이터가 영구적으로 삭제됩니다.`)) return;
    fetch('/admin/delete-course', { method: 'POST', headers: {'Content-Type':'application/x-www-form-urlencoded'}, body: `course_id=${courseId}` })
    .then(r => { if(r.ok) { alert("삭제되었습니다."); goBackToDashboard(); } else alert("삭제 실패"); });
}
function showCourseCodeModal(code) {
    document.getElementById('generated-course-code').innerText = code;
    openModal('modal-course-code'); 
}
function copyCourseCode() {
    const code = document.getElementById('generated-course-code').innerText;
    if (navigator.clipboard) navigator.clipboard.writeText(code).then(() => alert("복사되었습니다.")).catch(()=>{});
}

/* Admin Views */
function switchAdminView(v){
    if(adminRefreshInterval) clearInterval(adminRefreshInterval);
    document.querySelectorAll('.content-view').forEach(e=>e.classList.add('hidden'));
    document.querySelectorAll('.nav-item').forEach(e=>e.classList.remove('active'));
    
    if(v==='dashboard'){
        document.getElementById('view-dashboard').classList.remove('hidden');
        document.getElementById('nav-dashboard').classList.add('active');
        loadAllRecords(currentAdminViewDate);
        adminRefreshInterval=setInterval(()=>{
            const t=new Date(), ts=`${t.getFullYear()}-${String(t.getMonth()+1).padStart(2,'0')}-${String(t.getDate()).padStart(2,'0')}`;
            if(currentAdminViewDate===ts) loadAllRecords(currentAdminViewDate);
        },3000);
    } else if(v==='students'){
        document.getElementById('view-students').classList.remove('hidden');
        document.getElementById('nav-students').classList.add('active');
        loadEnrolledStudents();
    } else if(v==='settings'){
        document.getElementById('view-settings').classList.remove('hidden');
        document.getElementById('nav-settings').classList.add('active');
        loadSchedules(); loadExceptions();
    }
}

/* Students & Stats */
function loadEnrolledStudents(){
    const l=document.getElementById('enrolled-students-list'); l.innerHTML="<p style='text-align:center;'>로딩 중...</p>";
    fetch('/admin/get-enrolled-students',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`course_id=${currentCourseId}`})
    .then(r=>r.text()).then(d=>{
        if(!d){l.innerHTML="<p style='text-align:center;'>학생 없음</p>"; document.getElementById('student-count-title').innerText="학생 (0명)"; return;}
        const arr=d.split('|').filter(x=>x); document.getElementById('student-count-title').innerText=`학생 (${arr.length}명)`;
        let h=`<table class="admin-table"><thead><tr><th>학번</th><th>이름</th><th style="text-align:center;">관리</th></tr></thead><tbody>`;
        arr.forEach(r=>{
            const [sn,nm]=r.split(','); 
            h+=`<tr><td>${sn}</td><td>${nm}</td><td style="text-align:center;"><button class="small-btn" onclick="viewStudentStats('${sn}','${nm}')">📊 상세 통계</button></td></tr>`;
        });
        l.innerHTML=h+"</tbody></table>";
    });
}
function viewStudentStats(sn, nm) {
    openModal('modal-student-stats');
    document.getElementById('stat-student-name').innerText = `${nm} (${sn})님의 상세 기록`;
    fetch('/admin/student-stats', {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`student_number=${sn}&course_id=${currentCourseId}`})
    .then(res => res.text()).then(data => {
        if (!data) return;
        const parts = data.split('|');
        const [p, l, a] = parts[0].split(',').map(Number);
        const total = p + l + a;
        document.getElementById('stat-total').innerText = total + "회";
        document.getElementById('stat-present').innerText = p;
        document.getElementById('stat-late').innerText = l;
        document.getElementById('stat-absent').innerText = a;
        
        let rate = total > 0 ? Math.round(((p + l) / total) * 100) : 0;
        document.getElementById('stat-rate').innerText = rate + "%";
        const bar = document.getElementById('stat-bar');
        bar.style.width = rate + "%";
        bar.style.backgroundColor = rate >= 80 ? "#2ecc71" : (rate >= 50 ? "#f1c40f" : "#e74c3c");

        const renderList = (id, str) => {
            const list = document.getElementById(id); list.innerHTML = "";
            if(str && str.length > 1) str.split(',').filter(x=>x).forEach(d => { list.innerHTML += `<li>📅 ${d}</li>`; });
            else list.innerHTML = "<li style='text-align:center; color:#ccc; font-size:12px;'>기록 없음</li>";
        };
        renderList('stat-late-list', parts[1]);
        renderList('stat-absent-list', parts[2]);
    });
}

/* Schedule Management */
function loadExceptions() {
    const cList = document.getElementById('cancel-list'); cList.innerHTML = "<li>로딩 중...</li>";
    fetch('/admin/get-cancellations', {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`course_id=${currentCourseId}`})
    .then(res => res.text()).then(d => {
        cList.innerHTML = d ? "" : "<li style='color:#ccc; text-align:center;'>등록된 휴강 없음</li>";
        if(d) d.split('|').filter(x=>x).forEach(r => {
            const [id, date] = r.split(',');
            cList.innerHTML += `<li><span style="color:#e74c3c;">⛔ ${date}</span><button onclick="deleteCancellation(${id})" class="small-btn btn-danger" style="padding:2px 6px;">삭제</button></li>`;
        });
    });
    const sList = document.getElementById('special-list'); sList.innerHTML = "<li>로딩 중...</li>";
    fetch('/admin/get-specials', {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`course_id=${currentCourseId}`})
    .then(res => res.text()).then(d => {
        sList.innerHTML = d ? "" : "<li style='color:#ccc; text-align:center;'>등록된 보강 없음</li>";
        if(d) d.split('|').filter(x=>x).forEach(r => {
            const [id, date, s, l, e] = r.split(',');
            sList.innerHTML += `<li><div><span style="color:#2196F3;">➕ ${date}</span> <span style="font-size:12px;color:#666;">${s.substring(0,5)}~${e.substring(0,5)}</span></div><button onclick="deleteSpecial(${id})" class="small-btn btn-danger" style="padding:2px 6px;">삭제</button></li>`;
        });
    });
}
function deleteCancellation(id){ if(confirm("삭제?")) fetch('/admin/delete-cancellation',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`id=${id}`}).then(r=>{if(r.ok)loadExceptions();}); }
function deleteSpecial(id){ if(confirm("삭제?")) fetch('/admin/delete-special',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`id=${id}`}).then(r=>{if(r.ok)loadExceptions();}); }

function addSchedule() {
    const d=document.getElementById('sched-day').value, s=document.getElementById('sched-start').value, l=document.getElementById('sched-late').value, e=document.getElementById('sched-end').value, sd=document.getElementById('sched-valid-start').value, ed=document.getElementById('sched-valid-end').value;
    if(!s||!l||!e||!sd||!ed) return alert("모든 항목을 입력하세요.");
    fetch('/admin/add-schedule', {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`course_id=${currentCourseId}&day=${d}&start=${s}&late=${l}&end=${e}&start_date=${sd}&end_date=${ed}`})
    .then(r=>{if(r.ok){alert("추가됨");loadSchedules();closeModal('modal-add-schedule');}else alert("실패");});
}
function loadSchedules() {
    const l=document.getElementById('schedule-list'); l.innerHTML = "<li>로딩 중...</li>";
    fetch('/admin/get-schedules',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`course_id=${currentCourseId}`})
    .then(r=>r.text()).then(d => {
        l.innerHTML = d ? "" : "<li style='text-align:center; color:#ccc;'>시간표 없음</li>";
        if(d) d.split('|').filter(x=>x).forEach(r => {
            const [day, s, la, e, sd, ed, id] = r.split(',');
            l.innerHTML += `<li><div><span style="font-weight:bold;">${getDayName(day)}요일</span> ${s.substring(0,5)}~${e.substring(0,5)} <div style="font-size:11px;color:#888;">${sd}~${ed}</div></div><button onclick="deleteSchedule(${id})" style="border:none;background:#fee;color:red;padding:2px 5px;cursor:pointer;">삭제</button></li>`;
        });
    });
}
function deleteSchedule(id){ if(confirm("삭제?")) fetch('/admin/delete-schedule',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`schedule_id=${id}`}).then(r=>{if(r.ok)loadSchedules();}); }

function addCancellation(){
    const d = document.getElementById('cancel-date').value; 
    if(!d) return alert("날짜 선택");
    fetch('/admin/cancel-class', {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`course_id=${currentCourseId}&date=${d}`})
    .then(r=>r.text().then(m=>({ok:r.ok, m:m}))).then(res=>{
        if(res.ok){ alert("등록됨"); closeModal('modal-cancel'); loadExceptions(); }
        else if(res.m==="NoClass") alert("수업 없는 날입니다.");
        else alert("오류");
    });
}
function addSpecialSchedule(){
    const d=document.getElementById('special-date').value, s=document.getElementById('special-start').value, l=document.getElementById('special-late').value, e=document.getElementById('special-end').value;
    if(!d||!s||!l||!e) return alert("입력 확인");
    fetch('/admin/add-special', {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`course_id=${currentCourseId}&date=${d}&start=${s}&late=${l}&end=${e}`})
    .then(r=>r.text().then(m=>({ok:r.ok, m:m}))).then(res=>{
        if(res.ok){ alert("등록됨"); closeModal('modal-special'); loadExceptions(); }
        else if(res.m==="OverlapSpecial") alert("다른 보강과 겹침");
        else if(res.m==="OverlapRegular") alert("정규 수업과 겹침");
        else alert("오류");
    });
}

/* Records */
function loadAllRecords(date){
    if(!date){const t=new Date(); date=`${t.getFullYear()}-${String(t.getMonth()+1).padStart(2,'0')}-${String(t.getDate()).padStart(2,'0')}`;}
    currentAdminViewDate=date; 
    document.getElementById('list-date-title').innerText=`📋 ${date} 현황`;
    fetch('/admin/all-records',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`course_id=${currentCourseId}&date=${date}`})
    .then(r=>r.text()).then(d=>{
        const l=document.getElementById('student-list');
        if(!d){l.innerHTML="<p style='text-align:center;padding:20px'>기록 없음</p>";return;}
        let h=`<table class="admin-table"><thead><tr><th>학번</th><th>이름</th><th>시간</th><th>상태</th><th>관리</th></tr></thead><tbody>`;
        d.split('|').filter(x=>x).forEach(r=>{
            const [id,sn,nm,tm,st,isSpec]=r.split(',');
            let badge = `<span class="badge ${st==='지각'?'late':(st==='결석'?'absent':'present')}">${st}</span>`;
            if(isSpec==="1") badge += ` <span class="badge special-class">보강</span>`;
            h+=`<tr><td>${sn}</td><td>${nm}</td><td>${tm}</td><td>${badge}</td>
                <td><button onclick="changeStatus(${id},'${nm}','출석')" class="small-btn">출석</button> <button onclick="changeStatus(${id},'${nm}','결석')" class="small-btn">결석</button></td></tr>`;
        });
        l.innerHTML=h+"</tbody></table>";
    });
}
function changeStatus(rid, nm, st){
    if(!confirm(`${nm} -> ${st} 변경?`)) return;
    fetch('/admin/update-status', {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`record_id=${rid}&status=${st}`})
    .then(r => { if(r.ok) { alert("변경됨"); loadAllRecords(currentAdminViewDate); } else alert("실패"); });
}
function requestCheckIn(){
    const btn=document.querySelector('.check-in-circle');
    if(btn.style.pointerEvents==="none") return;
    btn.style.pointerEvents="none"; btn.style.opacity="0.7"; btn.innerText="처리중...";
    fetch('/check-in',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`student_number=${sessionStorage.getItem("student_id")}&course_id=${currentCourseId}`})
    .then(r=>r.text().then(t=>({ok:r.ok, t:t}))).then(r=>{
        if(r.ok) alert("✅ 출석 완료");
        else if(r.t==="Already") alert("⚠️ 이미 하셨습니다.");
        else if(r.t==="Cancelled") alert("⛔ 휴강입니다.");
        else if(r.t==="NotStarted") alert("⛔ 수업 시간이 아닙니다.");
        else alert("오류");
    })
    .finally(() => { setTimeout(() => { btn.style.pointerEvents="auto"; btn.style.opacity="1"; btn.innerText="출석하기"; }, 1000); });
}

/* Calendar */
function openAdminCalendar() {
    currentCalendarDate=new Date();
    fetch('/admin/calendar-meta',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`course_id=${currentCourseId}`})
    .then(r=>r.text()).then(d=>{ parseScheduleData(d); renderCalendar(true); openModal('calendar-modal'); });
}
function openCalendar(){
    currentCalendarDate=new Date();
    fetch('/get-attendance-dates',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`student_number=${sessionStorage.getItem("student_id")}&course_id=${currentCourseId}`})
    .then(r=>r.text()).then(att=>{
        allAttendedTimes={}; 
        att.split('^').filter(x=>x).forEach(r=>{
            const [dt,st,isSpec]=r.split(',');
            const dateKey = dt.substring(0,10);
            const infoText = `${dt} (${st})` + (isSpec==="1"?" [보강]":" [정규]");

            if (allAttendedTimes[dateKey]) {
                allAttendedTimes[dateKey] += "\n" + infoText;
            } else {
                allAttendedTimes[dateKey] = infoText;
            }
        });
        renderCalendar(false); openModal('calendar-modal');
    });
}
function parseScheduleData(data) {
    courseScheduleData = { weekly: [], cancel: [], special: [] };
    if(!data) return;
    data.split('|').forEach(p => {
        if(p.startsWith("WEEKLY:")) p.replace("WEEKLY:","").split('^').filter(x=>x).forEach(i=>{const[d,s,e]=i.split(','); courseScheduleData.weekly.push({day:parseInt(d),start:s,end:e});});
        else if(p.startsWith("CANCEL:")) courseScheduleData.cancel=p.replace("CANCEL:","").split(',').filter(x=>x);
        else if(p.startsWith("SPECIAL:")) courseScheduleData.special=p.replace("SPECIAL:","").split(',').filter(x=>x);
    });
}
function renderCalendar(isAdmin) {
    const g=document.getElementById('calendar-grid'); g.innerHTML="";
    const y=currentCalendarDate.getFullYear(), m=currentCalendarDate.getMonth();
    document.getElementById('cal-month-year').innerText=`${y}년 ${m+1}월`;
    const f=new Date(y,m,1).getDay(), l=new Date(y,m+1,0).getDate();
    for(let i=0;i<f;i++)g.appendChild(document.createElement('div'));
    for(let d=1;d<=l;d++){
        const div=document.createElement('div'); div.className='day'; div.innerHTML=`<span>${d}</span>`;
        const dateStr=`${y}-${String(m+1).padStart(2,'0')}-${String(d).padStart(2,'0')}`;
        const dayOfWeek=new Date(y,m,d).getDay();
        let isReg=courseScheduleData.weekly.some(w=>w.day===dayOfWeek && dateStr>=w.start && dateStr<=w.end);
        let isSpec=courseScheduleData.special.includes(dateStr), isCancel=courseScheduleData.cancel.includes(dateStr);
        
        if(isAdmin){
            if(isCancel) { div.classList.add('is-canceled'); div.innerHTML+=`<span class="cancel-mark">휴강</span>`; }
            else {
                let h=`<div class="dot-container">`;
                if(isReg) h+=`<span class="cal-dot dot-regular"></span>`;
                if(isSpec) h+=`<span class="cal-dot dot-special"></span>`;
                div.innerHTML += h+`</div>`;
            }
            div.onclick=()=>{loadAllRecords(dateStr); closeModal('calendar-modal');};
        } else {
            if(isCancel) { 
                div.classList.add('is-canceled'); 
                div.innerHTML+=`<span class="cancel-mark">휴강</span>`;}
            else if(allAttendedTimes[dateStr]) {
                const s=allAttendedTimes[dateStr];
                if (s.includes("결석")) {
                    div.classList.add('status-absent');
                } else if (s.includes("지각")) {
                    div.classList.add('status-late');
                } else {
                    div.classList.add('status-present');
                }

                div.onclick=()=>{
                    document.getElementById('detail-date').innerText=dateStr; 
                    document.getElementById('detail-time').innerHTML = s.replace(/\n/g, "<br>"); 
                    document.getElementById('detail-modal').classList.remove('hidden');
                };
            } else if(isReg||isSpec) {
                div.innerHTML+=`<div class="dot-container"><span class="cal-dot dot-future"></span></div>`;
                if(dateStr===new Date().toISOString().split('T')[0]) { div.style.border="2px solid #3498db"; div.onclick=()=>{closeModal('calendar-modal'); requestCheckIn();}; }
            }
        }
        g.appendChild(div);
    }
}
function closeModal(id){document.getElementById(id).classList.add('hidden');}
function openModal(id){document.getElementById(id).classList.remove('hidden');}
function closeCalendar(){closeModal('calendar-modal');}
function getDayName(d){return ["일","월","화","수","목","금","토"][d];}
function goToPrevMonth(){currentCalendarDate.setMonth(currentCalendarDate.getMonth()-1);renderCalendar(sessionStorage.getItem("is_admin")==="1");}
function goToNextMonth(){currentCalendarDate.setMonth(currentCalendarDate.getMonth()+1);renderCalendar(sessionStorage.getItem("is_admin")==="1");}
function showMonthYearPicker(){document.getElementById('month-year-picker').classList.remove('hidden');}
function applyMonthYearSelection(){
    const y=document.getElementById('year-select').value, m=document.getElementById('month-select').value;
    currentCalendarDate=new Date(y,m,1); document.getElementById('month-year-picker').classList.add('hidden'); renderCalendar(sessionStorage.getItem("is_admin")==="1");
}
function populateMonthYearPicker(){
    const ys=document.getElementById('year-select'), ms=document.getElementById('month-select'), c=new Date().getFullYear();
    for(let y=c-5;y<=c+5;y++)ys.add(new Option(y+'년',y)); for(let m=0;m<12;m++)ms.add(new Option((m+1)+'월',m));
}