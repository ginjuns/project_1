CREATE DATABASE IF NOT EXISTS project 
    DEFAULT CHARACTER SET utf8mb4 
    COLLATE utf8mb4_unicode_ci;
USE project;

CREATE TABLE users (
    student_number BIGINT PRIMARY KEY,  #학번
    username VARCHAR(100) NOT NULL,         #사용자 이름
    pw VARCHAR(256) NOT NULL,               #비밀번호
    is_admin TINYINT DEFAULT 0              #권한 (1: 관리자, 0: 학생)
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

CREATE TABLE class (
    id INT AUTO_INCREMENT PRIMARY KEY,
    class_name VARCHAR(100) NOT NULL,        #강의명
    class_code VARCHAR(20) UNIQUE NOT NULL,	 #수업 코드
    admin_id BIGINT,                         #관리자 학번
    FOREIGN KEY (admin_id) REFERENCES users(student_number) ON DELETE CASCADE
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

CREATE TABLE enrollments (
    id INT AUTO_INCREMENT PRIMARY KEY,
    student_number BIGINT,                  #학번
    class_id INT,                           #수업 ID
    joined_at DATETIME DEFAULT CURRENT_TIMESTAMP, #수업 신청 시간
    
    FOREIGN KEY (student_number) REFERENCES users(student_number) ON DELETE CASCADE,
    FOREIGN KEY (class_id) REFERENCES class(id) ON DELETE CASCADE,
    UNIQUE KEY unique_enrollment (student_number, class_id) #중복 수강 방지
);

CREATE TABLE weekly_time (
    id INT AUTO_INCREMENT PRIMARY KEY,
    class_id INT,
    day_of_week INT NOT NULL,   #요일 (0:일요일 ~ 6:토요일)
    start_time TIME NOT NULL,   #출석 인정 시간
    late_time TIME NOT NULL,    #지각 처리 시간
    end_time TIME NOT NULL,     #수업 종료 시간
    start_date DATE NOT NULL,   #학기 시작일
    end_date DATE NOT NULL,     #학기 종료일
    
    FOREIGN KEY (class_id) REFERENCES class(id) ON DELETE CASCADE
);

CREATE TABLE class_cancel (
    id INT AUTO_INCREMENT PRIMARY KEY,
    class_id INT,
    cancel_date DATE NOT NULL,  #휴강 날짜
    
    FOREIGN KEY (class_id) REFERENCES class(id) ON DELETE CASCADE
);

CREATE TABLE class_plus (
    id INT AUTO_INCREMENT PRIMARY KEY,
    class_id INT,
    class_date DATE NOT NULL,   #보강 날짜
    start_time TIME NOT NULL,	#출석 인정 시간
    late_time TIME NOT NULL,	#지각 처리 시간
    end_time TIME NOT NULL,		#수업 종료 시간
    
    FOREIGN KEY (class_id) REFERENCES class(id) ON DELETE CASCADE
);

CREATE TABLE record (
    id INT AUTO_INCREMENT PRIMARY KEY,
    student_number BIGINT,
    class_id INT,
    conditions VARCHAR(20) NOT NULL,             	#상태 (출석, 지각, 결석)
    check_times DATETIME DEFAULT CURRENT_TIMESTAMP, #실제 체크 시간
    class_conditions TINYINT DEFAULT 0,             #수업 구분 (0: 정규 수업, 1: 보강)
    
    FOREIGN KEY (student_number) REFERENCES users(student_number) ON DELETE CASCADE,
    FOREIGN KEY (class_id) REFERENCES class(id) ON DELETE CASCADE
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;