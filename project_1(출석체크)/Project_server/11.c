#define _CRT_SECURE_NO_WARNINGS
#include "D:/J.H/sample2/Common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mysql.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "libmysql.lib") 

#define SERVERPORT    9000
#define RECV_BUFSIZE  8192

// DB 설정
#define DB_HOST "127.0.0.1"
#define DB_USER "root"
#define DB_PASS "1234"
#define DB_NAME "project" 
#define DB_PORT 3306
#define ADMIN_SECRET_KEY "PROF2025" 

CRITICAL_SECTION cs_db;
MYSQL* g_conn = NULL;

//도와주는 함수 모음
//강의 코드 무작위 생성
void generate_random_code(char* buf, int len) {
    const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < len; i++) buf[i] = charset[rand() % (sizeof(charset) - 1)];
    buf[len] = '\0';
}
//메모리 관리
static char* my_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* p = (char*)malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}
//한글 깨지는 현상 방지
void send_response(SOCKET s, const char* status, const char* content_type, const char* body) {
    char header[1024];
    int body_len = (int)strlen(body);
    snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\nContent-Type: %s; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
        status, content_type, body_len);
    send(s, header, strlen(header), 0);
    send(s, body, body_len, 0);
}
//파일 읽기
char* read_file_alloc(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}
//URL 인코딩된 문자열(예: %20 -> 공백)을 사람이 읽을 수 있게 변환
void url_decode(char* dst, const char* src) {
    while (*src) {
        if (*src == '+') { *dst++ = ' '; src++; }
        else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else { *dst++ = *src++; }
    }
    *dst = '\0';
}
//특정 키 값을 찾아 값을 추출(문자열, 정수, 더 큰 정수)
void form_parse(const char* body, const char* key, char* dest, int maxlen) {
    dest[0] = '\0';
    const char* p = body;
    int keylen = (int)strlen(key);
    while (p && *p) {
        const char* eq = strchr(p, '=');
        if (!eq) break;
        if ((int)(eq - p) == keylen && strncmp(p, key, keylen) == 0) {
            const char* val = eq + 1;
            const char* amp = strchr(val, '&');
            int vlen = amp ? (int)(amp - val) : (int)strlen(val);
            if (vlen >= maxlen) vlen = maxlen - 1;
            char tmp[65536];
            if (vlen >= (int)sizeof(tmp)) vlen = (int)sizeof(tmp) - 1;
            memcpy(tmp, val, vlen);
            tmp[vlen] = '\0';
            url_decode(dest, tmp);
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

long long form_parse_ll(const char* body, const char* key) {
    char str_val[128]; form_parse(body, key, str_val, sizeof(str_val));
    return strtoll(str_val, NULL, 10);
}

int form_parse_int(const char* body, const char* key) {
    char str_val[128]; form_parse(body, key, str_val, sizeof(str_val));
    return atoi(str_val);
}

//DB 함수
//학생별 출석 통계 조회
char* db_get_student_detail_stats(MYSQL* conn, long long sn, int cid) {
    char query[1024];
    snprintf(query, sizeof(query),
        "SELECT conditions, DATE_FORMAT(check_times, '%%Y-%%m-%%d %%H:%%i') "
        "FROM record WHERE student_number=%lld AND class_id=%d ORDER BY check_times DESC", sn, cid);
    if (mysql_query(conn, query)) return my_strdup("");
    MYSQL_RES* res = mysql_store_result(conn);
    MYSQL_ROW row;
    int p_count = 0, l_count = 0, a_count = 0;
    char* l_dates = (char*)malloc(30000); l_dates[0] = '\0';
    char* a_dates = (char*)malloc(30000); a_dates[0] = '\0';
    while ((row = mysql_fetch_row(res))) {
        char* st = row[0];
        char* date = row[1];
        if (strcmp(st, "\xEC\xB6\x9C\xEC\x84\x9D") == 0) { p_count++; }
        else if (strcmp(st, "\xEC\xA7\x80\xEA\xB0\x81") == 0) { l_count++; strcat(l_dates, date); strcat(l_dates, ","); }
        else { a_count++; strcat(a_dates, date); strcat(a_dates, ","); }
    }
    mysql_free_result(res);
    char* result = (char*)malloc(65536);
    snprintf(result, 65536, "%d,%d,%d|%s|%s", p_count, l_count, a_count, l_dates, a_dates);
    free(l_dates); free(a_dates);
    return result;
}
//회원가입
int db_register_user(MYSQL* conn, long long sn, const char* username, const char* pw, const char* admin_key) {
    char esc_name[201], esc_pw[512];
    mysql_real_escape_string(conn, esc_name, username, strlen(username));
    mysql_real_escape_string(conn, esc_pw, pw, strlen(pw));
    int is_admin = (strcmp(admin_key, ADMIN_SECRET_KEY) == 0) ? 1 : 0;
    char query[1024];
    snprintf(query, sizeof(query), "INSERT INTO users(student_number, username, pw, is_admin) VALUES(%lld, '%s', '%s', %d)", sn, esc_name, esc_pw, is_admin);
    if (mysql_query(conn, query) != 0) { printf("[Register Error] %s\n", mysql_error(conn)); return 0; }
    return 1;
}
//로그인
char* db_login_check(MYSQL* conn, long long sn, const char* pw) {
    char esc_pw[512];
    mysql_real_escape_string(conn, esc_pw, pw, strlen(pw));
    char query[1024];
    snprintf(query, sizeof(query), "SELECT username, is_admin FROM users WHERE student_number=%lld AND pw='%s'", sn, esc_pw);
    if (mysql_query(conn, query)) return NULL;
    MYSQL_RES* res = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(res);
    char* ret = NULL;
    if (row) { char buf[256]; snprintf(buf, sizeof(buf), "%s,%s", row[0], row[1]); ret = my_strdup(buf); }
    mysql_free_result(res);
    return ret;
}
//수업 생성
char* db_create_course(MYSQL* conn, long long prof_id, const char* name) {
    char code[10] = { 0 }, esc_name[256];
    mysql_real_escape_string(conn, esc_name, name, strlen(name));
    char query[1024];
    int max_attempts = 10; int code_unique = 0;
    MYSQL_RES* res = NULL;
    for (int i = 0; i < max_attempts; i++) {
        generate_random_code(code, 6);
        snprintf(query, sizeof(query), "SELECT 1 FROM class WHERE class_code='%s'", code);
        if (mysql_query(conn, query) == 0) {
            res = mysql_store_result(conn);
            if (mysql_num_rows(res) == 0) { code_unique = 1; mysql_free_result(res); break; }
            mysql_free_result(res);
        }
        else return NULL;
    }
    if (!code_unique) return NULL;
    snprintf(query, sizeof(query), "INSERT INTO class(class_name, class_code, admin_id) VALUES('%s', '%s', %lld)", esc_name, code, prof_id);
    if (mysql_query(conn, query) != 0) return NULL;
    return my_strdup(code);
}
//수업 참여
int db_join_course(MYSQL* conn, long long sn, const char* code) {
    char esc_code[16]; mysql_real_escape_string(conn, esc_code, code, strlen(code));
    char q1[512]; snprintf(q1, sizeof(q1), "SELECT id FROM class WHERE class_code='%s'", esc_code);
    if (mysql_query(conn, q1)) return 0;
    MYSQL_RES* res = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return 0; }
    int cid = atoi(row[0]);
    mysql_free_result(res);
    char q2[512]; snprintf(q2, sizeof(q2), "INSERT INTO enrollments(student_number, class_id, joined_at) VALUES(%lld, %d, NOW())", sn, cid);
    if (mysql_query(conn, q2)) return 0;
    return 1;
}
//수업 목록 조회
char* db_get_my_courses(MYSQL* conn, long long sn, int is_admin) {
    char query[2048];
    const char* sched_subquery = "(SELECT GROUP_CONCAT(CONCAT(day_of_week, '@', DATE_FORMAT(start_time, '%H:%i'), '@', DATE_FORMAT(end_time, '%H:%i')) SEPARATOR '&') FROM weekly_time WHERE class_id = c.id)";
    if (is_admin) {
        snprintf(query, sizeof(query), "SELECT c.id, c.class_name, c.class_code, u.username, IFNULL(%s, '') FROM class c JOIN users u ON c.admin_id = u.student_number WHERE c.admin_id=%lld", sched_subquery, sn);
    }
    else {
        snprintf(query, sizeof(query), "SELECT c.id, c.class_name, c.class_code, u.username, IFNULL(%s, '') FROM class c JOIN enrollments e ON c.id = e.class_id JOIN users u ON c.admin_id = u.student_number WHERE e.student_number=%lld", sched_subquery, sn);
    }
    if (mysql_query(conn, query)) return my_strdup("");
    MYSQL_RES* res = mysql_store_result(conn);
    char* buf = (char*)malloc(65536); buf[0] = '\0';
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        char line[1024];
        snprintf(line, sizeof(line), "%s,%s,%s,%s,%s|", row[0], row[1], row[2], row[3] ? row[3] : "Unknown", row[4] ? row[4] : "");
        strcat(buf, line);
    }
    mysql_free_result(res); return buf;
}
//정규 시간표 등록
int db_add_schedule(MYSQL* conn, int cid, int day, const char* s, const char* l, const char* e, const char* sd, const char* ed) {
    char query[1024];
    snprintf(query, sizeof(query), "INSERT INTO weekly_time(class_id, day_of_week, start_time, late_time, end_time, start_date, end_date) VALUES(%d, %d, '%s', '%s', '%s', '%s', '%s')", cid, day, s, l, e, sd, ed);
    if (mysql_query(conn, query) != 0) return 0;
    return 1;
}
//휴강 등록
int db_add_cancellation(MYSQL* conn, int cid, const char* date) {
    char checkQ[1024];
    snprintf(checkQ, sizeof(checkQ), "SELECT 1 FROM weekly_time WHERE class_id=%d AND day_of_week = (DAYOFWEEK('%s') - 1) AND '%s' BETWEEN start_date AND end_date", cid, date, date);
    if (mysql_query(conn, checkQ)) return 0;
    MYSQL_RES* res = mysql_store_result(conn);
    int exists = (int)mysql_num_rows(res);
    mysql_free_result(res);
    if (exists == 0) return -1;
    char query[1024]; snprintf(query, sizeof(query), "INSERT INTO class_cancel(class_id, cancel_date) VALUES(%d, '%s')", cid, date);
    return (mysql_query(conn, query) == 0) ? 1 : 0;
}
//보강 등록
int db_add_special_schedule(MYSQL* conn, int cid, const char* date, const char* s, const char* l, const char* e) {
    MYSQL_RES* res;
    char qSpec[1024]; snprintf(qSpec, sizeof(qSpec), "SELECT 1 FROM class_plus WHERE class_id=%d AND class_date='%s' AND start_time < '%s' AND end_time > '%s'", cid, date, e, s);
    if (mysql_query(conn, qSpec)) return 0;
    res = mysql_store_result(conn); if (mysql_num_rows(res) > 0) { mysql_free_result(res); return -1; } mysql_free_result(res);
    int is_cancelled = 0; char qCancel[512]; snprintf(qCancel, sizeof(qCancel), "SELECT 1 FROM class_cancel WHERE class_id=%d AND cancel_date='%s'", cid, date);
    if (mysql_query(conn, qCancel) == 0) { res = mysql_store_result(conn); if (mysql_num_rows(res) > 0) is_cancelled = 1; mysql_free_result(res); }
    if (!is_cancelled) {
        char qReg[1024]; snprintf(qReg, sizeof(qReg), "SELECT 1 FROM weekly_time WHERE class_id=%d AND day_of_week = (DAYOFWEEK('%s') - 1) AND '%s' BETWEEN start_date AND end_date AND start_time < '%s' AND end_time > '%s'", cid, date, date, e, s);
        if (mysql_query(conn, qReg)) return 0; res = mysql_store_result(conn); if (mysql_num_rows(res) > 0) { mysql_free_result(res); return -2; } mysql_free_result(res);
    }
    char query[1024]; snprintf(query, sizeof(query), "INSERT INTO class_plus(class_id, class_date, start_time, late_time, end_time) VALUES(%d, '%s', '%s', '%s', '%s')", cid, date, s, l, e);
    return (mysql_query(conn, query) == 0) ? 1 : 0;
}

char* db_get_schedules(MYSQL* conn, int cid) {
    char query[1024]; snprintf(query, sizeof(query), "SELECT day_of_week, start_time, late_time, end_time, start_date, end_date, id FROM weekly_time WHERE class_id=%d ORDER BY day_of_week, start_time", cid);
    if (mysql_query(conn, query)) return my_strdup("");
    MYSQL_RES* res = mysql_store_result(conn);
    char* buf = (char*)malloc(65536); buf[0] = '\0';
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) { char line[512]; snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,%s,%s|", row[0], row[1], row[2], row[3], row[4], row[5], row[6]); strcat(buf, line); }
    mysql_free_result(res); return buf;
}
//정규 시간표 삭제
int db_delete_schedule(MYSQL* conn, int sid) {
    char query[256]; snprintf(query, sizeof(query), "DELETE FROM weekly_time WHERE id=%d", sid);
    return (mysql_query(conn, query) == 0);
}

char* db_get_enrolled_students(MYSQL* conn, int cid) {
    char query[1024]; snprintf(query, sizeof(query), "SELECT u.student_number, u.username FROM users u JOIN enrollments e ON u.student_number = e.student_number WHERE e.class_id=%d", cid);
    if (mysql_query(conn, query)) return my_strdup("");
    MYSQL_RES* res = mysql_store_result(conn);
    char* buf = (char*)malloc(65536); buf[0] = '\0';
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) { char line[512]; snprintf(line, sizeof(line), "%s,%s|", row[0], row[1]); strcat(buf, line); }
    mysql_free_result(res); return buf;
}
//출석 체크
int db_check_in(MYSQL* conn, long long sn, int cid) {
    char start_t[10], late_t[10], end_t[10]; int found_schedule = 0; int is_special = 0; MYSQL_RES* r;
    char qSpec[1024]; snprintf(qSpec, sizeof(qSpec), "SELECT start_time, late_time, end_time FROM class_plus WHERE class_id=%d AND class_date=CURDATE() AND CURTIME() BETWEEN start_time AND end_time LIMIT 1", cid);
    if (mysql_query(conn, qSpec) == 0) { r = mysql_store_result(conn); MYSQL_ROW row = mysql_fetch_row(r); if (row) { strcpy(start_t, row[0]); strcpy(late_t, row[1]); strcpy(end_t, row[2]); found_schedule = 1; is_special = 1; } mysql_free_result(r); }
    if (!found_schedule) {
        char qCancel[512]; snprintf(qCancel, sizeof(qCancel), "SELECT 1 FROM class_cancel WHERE class_id=%d AND cancel_date=CURDATE()", cid);
        if (mysql_query(conn, qCancel) == 0) { r = mysql_store_result(conn); if (mysql_num_rows(r) > 0) { mysql_free_result(r); return -4; } mysql_free_result(r); }
        char qReg[1024]; snprintf(qReg, sizeof(qReg), "SELECT start_time, late_time, end_time FROM weekly_time WHERE class_id=%d AND day_of_week = (DAYOFWEEK(NOW()) - 1) AND CURDATE() BETWEEN start_date AND end_date AND CURTIME() BETWEEN start_time AND end_time ORDER BY start_time ASC LIMIT 1", cid);
        if (mysql_query(conn, qReg) == 0) { r = mysql_store_result(conn); MYSQL_ROW row = mysql_fetch_row(r); if (row) { strcpy(start_t, row[0]); strcpy(late_t, row[1]); strcpy(end_t, row[2]); found_schedule = 1; is_special = 0; } mysql_free_result(r); }
    }
    if (!found_schedule) return -3;
    char qDup[1024]; snprintf(qDup, sizeof(qDup), "SELECT 1 FROM record WHERE student_number=%lld AND class_id=%d AND DATE(check_times) = CURDATE() AND TIME(check_times) >= '%s' AND TIME(check_times) <= '%s' LIMIT 1", sn, cid, start_t, end_t);
    if (mysql_query(conn, qDup)) return 0; r = mysql_store_result(conn); if (mysql_num_rows(r) > 0) { mysql_free_result(r); return -2; } mysql_free_result(r);
    char qIns[2048]; snprintf(qIns, sizeof(qIns), "INSERT INTO record(student_number, class_id, conditions, class_conditions) SELECT %lld, %d, CASE WHEN CURTIME() <= '%s' THEN '\xEC\xB6\x9C\xEC\x84\x9D' ELSE '\xEC\xA7\x80\xEA\xB0\x81' END, %d", sn, cid, late_t, is_special);
    if (mysql_query(conn, qIns) != 0) return 0;
    return 1;
}
//기록 조회
char* db_get_records(MYSQL* conn, int cid, const char* date_str) {
    char query[2048]; snprintf(query, sizeof(query), "SELECT r.id, u.student_number, u.username, DATE_FORMAT(r.check_times,'%%H:%%i:%%s'), r.conditions, r.class_conditions FROM record r JOIN users u ON r.student_number=u.student_number WHERE r.class_id=%d AND DATE(r.check_times)='%s' ORDER BY r.check_times DESC", cid, date_str);
    if (mysql_query(conn, query)) return my_strdup("");
    MYSQL_RES* res = mysql_store_result(conn); char* buf = (char*)malloc(65536); buf[0] = '\0'; MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) { char line[512]; snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,%s|", row[0], row[1], row[2], row[3], row[4], row[5]); strcat(buf, line); } mysql_free_result(res); return buf;
}

int db_update_status(MYSQL* conn, int record_id, const char* st) {
    char query[1024]; snprintf(query, sizeof(query), "UPDATE record SET conditions='%s' WHERE id=%d", st, record_id);
    return (mysql_query(conn, query) == 0);
}

char* db_get_att_dates(MYSQL* conn, long long sn, int cid) {
    char query[1024]; snprintf(query, sizeof(query), "SELECT DATE_FORMAT(check_times, '%%Y-%%m-%%d %%H:%%i:%%s'), conditions, class_conditions FROM record WHERE student_number=%lld AND class_id=%d", sn, cid);
    if (mysql_query(conn, query)) return my_strdup("");
    MYSQL_RES* res = mysql_store_result(conn); char* buf = (char*)malloc(65536); buf[0] = '\0'; MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) { char line[256]; snprintf(line, sizeof(line), "%s,%s,%s^", row[0], row[1], row[2]); strcat(buf, line); } mysql_free_result(res); return buf;
}

char* db_get_calendar_meta(MYSQL* conn, int cid) {
    char* buf = (char*)malloc(65536); buf[0] = '\0'; char query[1024]; MYSQL_RES* res; MYSQL_ROW row;
    strcat(buf, "WEEKLY:"); snprintf(query, sizeof(query), "SELECT day_of_week, start_date, end_date FROM weekly_time WHERE class_id=%d", cid);
    if (mysql_query(conn, query) == 0) { res = mysql_store_result(conn); while ((row = mysql_fetch_row(res))) { strcat(buf, row[0]); strcat(buf, ","); strcat(buf, row[1]); strcat(buf, ","); strcat(buf, row[2]); strcat(buf, "^"); } mysql_free_result(res); }
    strcat(buf, "|CANCEL:"); snprintf(query, sizeof(query), "SELECT cancel_date FROM class_cancel WHERE class_id=%d", cid);
    if (mysql_query(conn, query) == 0) { res = mysql_store_result(conn); while ((row = mysql_fetch_row(res))) { strcat(buf, row[0]); strcat(buf, ","); } mysql_free_result(res); }
    strcat(buf, "|SPECIAL:"); snprintf(query, sizeof(query), "SELECT class_date FROM class_plus WHERE class_id=%d", cid);
    if (mysql_query(conn, query) == 0) { res = mysql_store_result(conn); while ((row = mysql_fetch_row(res))) { strcat(buf, row[0]); strcat(buf, ","); } mysql_free_result(res); }
    return buf;
}

char* db_get_cancellations(MYSQL* conn, int cid) {
    char query[1024]; snprintf(query, sizeof(query), "SELECT id, cancel_date FROM class_cancel WHERE class_id=%d ORDER BY cancel_date", cid);
    if (mysql_query(conn, query)) return my_strdup("");
    MYSQL_RES* res = mysql_store_result(conn); char* buf = (char*)malloc(65536); buf[0] = '\0'; MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) { char line[256]; snprintf(line, sizeof(line), "%s,%s|", row[0], row[1]); strcat(buf, line); } mysql_free_result(res); return buf;
}

int db_delete_cancellation(MYSQL* conn, int id) { char query[256]; snprintf(query, sizeof(query), "DELETE FROM class_cancel WHERE id=%d", id); return (mysql_query(conn, query) == 0); }

char* db_get_specials(MYSQL* conn, int cid) {
    char query[1024]; snprintf(query, sizeof(query), "SELECT id, class_date, start_time, late_time, end_time FROM class_plus WHERE class_id=%d ORDER BY class_date, start_time", cid);
    if (mysql_query(conn, query)) return my_strdup("");
    MYSQL_RES* res = mysql_store_result(conn); char* buf = (char*)malloc(65536); buf[0] = '\0'; MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) { char line[256]; snprintf(line, sizeof(line), "%s,%s,%s,%s,%s|", row[0], row[1], row[2], row[3], row[4]); strcat(buf, line); } mysql_free_result(res); return buf;
}

int db_delete_course(MYSQL* conn, int cid) { char query[1024]; snprintf(query, sizeof(query), "DELETE FROM class WHERE id=%d", cid); return (mysql_query(conn, query) == 0); }
int db_delete_special(MYSQL* conn, int id) { char query[256]; snprintf(query, sizeof(query), "DELETE FROM class_plus WHERE id=%d", id); return (mysql_query(conn, query) == 0); }

//자동 결석 처리
DWORD WINAPI BackgroundAttendanceService(LPVOID arg) {
    printf("[시스템] 백그라운드 출석 서비스가 시작되었습니다...\n");
    while (1) {
        Sleep(10000);
        EnterCriticalSection(&cs_db);

        // 1. 정규 수업 결석 처리
        char query1[4096];
        snprintf(query1, sizeof(query1),
            "INSERT INTO record (student_number, class_id, conditions, check_times, class_conditions) "
            "SELECT e.student_number, s.class_id, '\xEA\xB2\xB0\xEC\x84\x9D', TIMESTAMP(CURDATE(), s.end_time), 0 "
            "FROM weekly_time s JOIN enrollments e ON s.class_id = e.class_id "
            "WHERE s.day_of_week = (DAYOFWEEK(NOW()) - 1) AND s.end_time < CURTIME() "
            "AND CURDATE() BETWEEN s.start_date AND s.end_date "
            "AND e.joined_at < TIMESTAMP(CURDATE(), s.end_time) "
            "AND NOT EXISTS (SELECT 1 FROM class_cancel c WHERE c.class_id = s.class_id AND c.cancel_date = CURDATE()) "
            "AND NOT EXISTS (SELECT 1 FROM record r WHERE r.student_number = e.student_number AND r.class_id = s.class_id "
            "AND DATE(r.check_times) = CURDATE() AND TIME(r.check_times) BETWEEN s.start_time AND s.end_time)");

        if (mysql_query(g_conn, query1) == 0) {
            long long aff = (long long)mysql_affected_rows(g_conn);
            if (aff > 0) printf("[자동] 정규 수업 결석 처리: %lld건이 추가되었습니다.\n", aff);
        }

        // 2. 보강 수업 결석 처리
        char query2[4096];
        snprintf(query2, sizeof(query2),
            "INSERT INTO record (student_number, class_id, conditions, check_times, class_conditions) "
            "SELECT e.student_number, s.class_id, '\xEA\xB2\xB0\xEC\x84\x9D', TIMESTAMP(CURDATE(), s.end_time), 1 "
            "FROM class_plus s JOIN enrollments e ON s.class_id = e.class_id "
            "WHERE s.class_date = CURDATE() AND s.end_time < CURTIME() "
            "AND e.joined_at < TIMESTAMP(CURDATE(), s.end_time) "
            "AND NOT EXISTS (SELECT 1 FROM record r WHERE r.student_number = e.student_number AND r.class_id = s.class_id "
            "AND DATE(r.check_times) = CURDATE() AND TIME(r.check_times) BETWEEN s.start_time AND s.end_time)");

        if (mysql_query(g_conn, query2) == 0) {
            long long aff = (long long)mysql_affected_rows(g_conn);
            if (aff > 0) printf("[자동] 보강 수업 결석 처리: %lld건이 추가되었습니다.\n", aff);
        }

        LeaveCriticalSection(&cs_db);
    }
    return 0;
}

// 클라이언트와 데이터 통신
DWORD WINAPI ProcessClient(LPVOID arg) {
    SOCKET cs = (SOCKET)arg;
    int retval;
    char recvbuf[RECV_BUFSIZE];
    struct sockaddr_in clientaddr;
    int addrlen;
    char addr[INET_ADDRSTRLEN];

    // 클라이언트 정보 얻기
    addrlen = sizeof(clientaddr);
    getpeername(cs, (struct sockaddr*)&clientaddr, &addrlen);
    inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));

    // HTTP 요청 수신
    retval = recv(cs, recvbuf, sizeof(recvbuf) - 1, 0);
    if (retval == SOCKET_ERROR) {
        err_display("recv()");
        closesocket(cs);
        return 0;
    }
    else if (retval == 0) {
        closesocket(cs);
        return 0;
    }

    recvbuf[retval] = '\0';

    // 요청 파싱
    char method[16] = { 0 }, path[256] = { 0 };
    sscanf(recvbuf, "%15s %255s", method, path);
    char* body = strstr(recvbuf, "\r\n\r\n");
    body = body ? body + 4 : "";

    // 로그 출력
    printf("[TCP/%s:%d] 요청 수신: %s %s\n", addr, ntohs(clientaddr.sin_port), method, path);

    EnterCriticalSection(&cs_db);

    //라우팅 및 DB 처리 로직
    if (!strcmp(method, "GET") && !strcmp(path, "/")) {
        char* p = read_file_alloc("index.html"); send_response(cs, "200 OK", "text/html", p ? p : ""); if (p) free(p);
    }
    else if (!strcmp(method, "GET") && !strcmp(path, "/style.css")) {
        char* p = read_file_alloc("style.css"); send_response(cs, "200 OK", "text/css", p ? p : ""); if (p) free(p);
    }
    else if (!strcmp(method, "GET") && !strcmp(path, "/script.js")) {
        char* p = read_file_alloc("script.js"); send_response(cs, "200 OK", "application/javascript", p ? p : ""); if (p) free(p);
    }
    else if (!strcmp(path, "/register")) {
        long long sn = form_parse_ll(body, "student_number");
        char nm[128], pw[256], ak[128];
        form_parse(body, "username", nm, sizeof(nm)); form_parse(body, "pw", pw, sizeof(pw)); form_parse(body, "admin_key", ak, sizeof(ak));
        int ok = db_register_user(g_conn, sn, nm, pw, ak); send_response(cs, ok ? "200 OK" : "400 Bad", "text/plain", ok ? "OK" : "Fail");
    }
    else if (!strcmp(path, "/login")) {
        long long sn = form_parse_ll(body, "student_number"); char pw[256]; form_parse(body, "pw", pw, sizeof(pw));
        char* info = db_login_check(g_conn, sn, pw);
        if (info) { send_response(cs, "200 OK", "text/plain", info); free(info); }
        else send_response(cs, "401 Unauthorized", "text/plain", "Fail");
    }
    else if (!strcmp(path, "/create-course")) {
        long long prof = form_parse_ll(body, "prof_id"); char name[128]; form_parse(body, "course_name", name, sizeof(name));
        char* code = db_create_course(g_conn, prof, name);
        if (code) { send_response(cs, "200 OK", "text/plain", code); free(code); }
        else send_response(cs, "400 Bad", "text/plain", "Error");
    }
    else if (!strcmp(path, "/join-course")) {
        long long sn = form_parse_ll(body, "student_number"); char code[32]; form_parse(body, "code", code, sizeof(code));
        int ok = db_join_course(g_conn, sn, code); send_response(cs, ok ? "200 OK" : "400 Bad", "text/plain", ok ? "OK" : "Fail");
    }
    else if (!strcmp(path, "/get-my-courses")) {
        long long sn = form_parse_ll(body, "student_number"); int is_admin = form_parse_int(body, "is_admin");
        char* list = db_get_my_courses(g_conn, sn, is_admin); send_response(cs, "200 OK", "text/plain", list); free(list);
    }
    else if (!strcmp(path, "/check-in")) {
        long long sn = form_parse_ll(body, "student_number"); int cid = form_parse_int(body, "course_id");
        int r = db_check_in(g_conn, sn, cid);
        if (r == 1) send_response(cs, "200 OK", "text/plain", "Success");
        else if (r == -2) send_response(cs, "400 Bad", "text/plain", "Already");
        else if (r == -3) send_response(cs, "400 Bad", "text/plain", "NotStarted");
        else if (r == -4) send_response(cs, "400 Bad", "text/plain", "Cancelled");
        else send_response(cs, "400 Bad", "text/plain", "Error");
    }
    else if (!strcmp(path, "/admin/add-schedule")) {
        int cid = form_parse_int(body, "course_id"); int day = form_parse_int(body, "day");
        char s[32], l[32], e[32], sd[32], ed[32];
        form_parse(body, "start", s, sizeof(s)); form_parse(body, "late", l, sizeof(l)); form_parse(body, "end", e, sizeof(e));
        form_parse(body, "start_date", sd, sizeof(sd)); form_parse(body, "end_date", ed, sizeof(ed));
        int ok = db_add_schedule(g_conn, cid, day, s, l, e, sd, ed);
        send_response(cs, ok ? "200 OK" : "400 Bad", "text/plain", ok ? "OK" : "Fail");
    }
    else if (!strcmp(path, "/admin/get-schedules")) {
        int cid = form_parse_int(body, "course_id"); char* list = db_get_schedules(g_conn, cid); send_response(cs, "200 OK", "text/plain", list); free(list);
    }
    else if (!strcmp(path, "/admin/delete-schedule")) {
        int sid = form_parse_int(body, "schedule_id"); db_delete_schedule(g_conn, sid); send_response(cs, "200 OK", "text/plain", "Deleted");
    }
    else if (!strcmp(path, "/admin/cancel-class")) {
        int cid = form_parse_int(body, "course_id"); char dt[32]; form_parse(body, "date", dt, sizeof(dt));
        int r = db_add_cancellation(g_conn, cid, dt);
        if (r == 1) send_response(cs, "200 OK", "text/plain", "OK");
        else if (r == -1) send_response(cs, "400 Bad", "text/plain", "NoClass");
        else send_response(cs, "400 Bad", "text/plain", "Fail");
    }
    else if (!strcmp(path, "/admin/add-special")) {
        int cid = form_parse_int(body, "course_id");
        char dt[32], s[32], l[32], e[32];
        form_parse(body, "date", dt, sizeof(dt)); form_parse(body, "start", s, sizeof(s)); form_parse(body, "late", l, sizeof(l)); form_parse(body, "end", e, sizeof(e));
        int r = db_add_special_schedule(g_conn, cid, dt, s, l, e);
        if (r == 1) send_response(cs, "200 OK", "text/plain", "OK");
        else if (r == -1) send_response(cs, "400 Bad", "text/plain", "OverlapSpecial");
        else if (r == -2) send_response(cs, "400 Bad", "text/plain", "OverlapRegular");
        else send_response(cs, "400 Bad", "text/plain", "Fail");
    }
    else if (!strcmp(path, "/admin/all-records")) {
        int cid = form_parse_int(body, "course_id"); char dt[32]; form_parse(body, "date", dt, sizeof(dt));
        char* list = db_get_records(g_conn, cid, dt); send_response(cs, "200 OK", "text/plain", list); free(list);
    }
    else if (!strcmp(path, "/admin/update-status")) {
        int record_id = form_parse_int(body, "record_id"); char st[64]; form_parse(body, "status", st, sizeof(st));
        int r = db_update_status(g_conn, record_id, st); send_response(cs, r ? "200 OK" : "400 Bad", "text/plain", r ? "Updated" : "Fail");
    }
    else if (!strcmp(path, "/admin/get-enrolled-students")) {
        int cid = form_parse_int(body, "course_id"); char* list = db_get_enrolled_students(g_conn, cid); send_response(cs, "200 OK", "text/plain", list); free(list);
    }
    else if (!strcmp(path, "/admin/calendar-meta")) {
        int cid = form_parse_int(body, "course_id"); char* meta = db_get_calendar_meta(g_conn, cid); send_response(cs, "200 OK", "text/plain", meta); free(meta);
    }
    else if (!strcmp(path, "/admin/delete-course")) {
        int cid = form_parse_int(body, "course_id"); int r = db_delete_course(g_conn, cid); send_response(cs, r ? "200 OK" : "400 Bad Request", "text/plain", r ? "Deleted" : "Fail");
    }
    else if (!strcmp(path, "/student/get-schedule")) {
        int cid = form_parse_int(body, "course_id"); char* meta = db_get_calendar_meta(g_conn, cid); send_response(cs, "200 OK", "text/plain", meta); free(meta);
    }
    else if (!strcmp(path, "/admin/get-cancellations")) {
        int cid = form_parse_int(body, "course_id"); char* list = db_get_cancellations(g_conn, cid); send_response(cs, "200 OK", "text/plain", list); free(list);
    }
    else if (!strcmp(path, "/admin/delete-cancellation")) {
        int id = form_parse_int(body, "id"); db_delete_cancellation(g_conn, id); send_response(cs, "200 OK", "text/plain", "Deleted");
    }
    else if (!strcmp(path, "/admin/get-specials")) {
        int cid = form_parse_int(body, "course_id"); char* list = db_get_specials(g_conn, cid); send_response(cs, "200 OK", "text/plain", list); free(list);
    }
    else if (!strcmp(path, "/admin/delete-special")) {
        int id = form_parse_int(body, "id"); db_delete_special(g_conn, id); send_response(cs, "200 OK", "text/plain", "Deleted");
    }
    else if (!strcmp(path, "/get-attendance-dates")) {
        long long sn = form_parse_ll(body, "student_number"); int cid = form_parse_int(body, "course_id");
        char* csv = db_get_att_dates(g_conn, sn, cid); send_response(cs, "200 OK", "text/plain", csv); free(csv);
    }
    else if (!strcmp(path, "/admin/student-stats")) {
        long long sn = form_parse_ll(body, "student_number"); int cid = form_parse_int(body, "course_id");
        char* stats = db_get_student_detail_stats(g_conn, sn, cid); send_response(cs, "200 OK", "text/plain", stats); free(stats);
    }
    else { send_response(cs, "404 Not Found", "text/plain", "Not Found"); }

    LeaveCriticalSection(&cs_db);

    // HTTP는 요청 처리 후 소켓 종료 (비연결 지향 처리)
    closesocket(cs);
    printf("[TCP 서버] 클라이언트 연결 종료: IP=%s, 포트=%d\n", addr, ntohs(clientaddr.sin_port));
    return 0;
}

int main(int argc, char* argv[])
{
    srand((unsigned)time(NULL));
    InitializeCriticalSection(&cs_db);

    int retval;

    // 윈속 초기화
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 1;

    // MySQL 초기화
    g_conn = mysql_init(NULL);
    if (!mysql_real_connect(g_conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0)) {
        printf("DB 연결 실패\n");
        return 1;
    }
    mysql_set_character_set(g_conn, "utf8mb4");

    // 백그라운드 서비스 시작
    HANDLE hBgThread = CreateThread(NULL, 0, BackgroundAttendanceService, NULL, 0, NULL);
    if (hBgThread) CloseHandle(hBgThread);

    // 소켓 생성
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) err_quit("socket()");

    // bind()
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(SERVERPORT);
    retval = bind(listen_sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
    if (retval == SOCKET_ERROR) err_quit("bind()");

    // listen()
    retval = listen(listen_sock, SOMAXCONN);
    if (retval == SOCKET_ERROR) err_quit("listen()");

    printf("출석 체크 서버 시작 (TCP 서버 모델 / 포트: %d)\n", SERVERPORT);

    // 데이터 통신에 사용할 변수
    SOCKET client_sock;
    struct sockaddr_in clientaddr;
    int addrlen;
    HANDLE hThread;

    while (1) {
        // accept()
        addrlen = sizeof(clientaddr);
        client_sock = accept(listen_sock, (struct sockaddr*)&clientaddr, &addrlen);
        if (client_sock == INVALID_SOCKET) {
            err_display("accept()");
            break;
        }

        // 접속한 클라이언트 정보 출력
        char addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));
        printf("\n[TCP 서버] 클라이언트 접속: IP=%s, 포트=%d\n",
            addr, ntohs(clientaddr.sin_port));

        // 스레드 생성
        hThread = CreateThread(NULL, 0, ProcessClient,
            (LPVOID)client_sock, 0, NULL);
        if (hThread == NULL) { closesocket(client_sock); }
        else { CloseHandle(hThread); }
    }

    // 소켓 닫기 및 정리 및 윈속 종료
    closesocket(listen_sock);
    WSACleanup();
    DeleteCriticalSection(&cs_db);
    mysql_close(g_conn);

    return 0;
}