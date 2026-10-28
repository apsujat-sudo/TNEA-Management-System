#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define GETCWD _getcwd
#define CHANGE_DIR _chdir
#else
#include <unistd.h>
#define GETCWD getcwd
#define CHANGE_DIR chdir
#endif

#define EMAIL_BUCKETS 101
#define NAME_LEN 60
#define EMAIL_LEN 80
#define PASS_LEN 40
#define TEXT_LEN 120
#define DATA_STUDENTS "tnea_ds_students.txt"
#define DATA_COLLEGES "tnea_ds_colleges.txt"
#define DATA_APPS "tnea_ds_applications.txt"
#define ADMIN_PASSWORD "admin123"

typedef struct Student Student;
typedef struct College College;
typedef struct Application Application;
typedef struct Preference Preference;
typedef struct EmailEntry EmailEntry;
typedef struct CollegeNode CollegeNode;
typedef struct HeapItem HeapItem;

/* Linked-list node for student records. */
struct Student {
    int id;
    char name[NAME_LEN], email[EMAIL_LEN], password[PASS_LEN];
    char phone[24], dob[16], address[TEXT_LEN];
    double physics, chemistry, mathematics, optionalMark, hscTotal, cutoff;
    char optionalSubject[16];
    int registered;
    Student *next;
};

/* Linked-list node for college records. */
struct College {
    int id;
    char name[NAME_LEN], location[60], branch[60];
    double cutoff;
    int seats, active;
    College *next;
};

/* Preferences are a linked list, not a fixed-size integer array. */
struct Preference {
    int collegeId;
    Preference *next;
};

/* Linked-list node for applications. */
struct Application {
    int id, studentId, submitted, allottedCollegeId;
    char status[24];
    Preference *choices;
    int choiceCount;
    Application *next;
};

/* Hash-table entry; collisions are handled by chaining. */
struct EmailEntry {
    char email[EMAIL_LEN];
    Student *student;
    EmailEntry *next;
};

/* BST node indexing colleges by college ID. */
struct CollegeNode {
    College *college;
    CollegeNode *left, *right;
};

/* Heap item used to rank students by cutoff. */
struct HeapItem {
    Student *student;
};

/* The primary record stores are linked lists. */
static Student *students = NULL;
static College *colleges = NULL;
static Application *applications = NULL;
static EmailEntry *emailTable[EMAIL_BUCKETS];
static CollegeNode *collegeRoot = NULL;

/* ---------- Safe input helpers ---------- */

static void readLine(const char *prompt, char *out, size_t size) {
    int ch;
    if (size == 0) return;
    printf("%s", prompt);
    if (!fgets(out, (int)size, stdin)) {
        out[0] = '\0';
        clearerr(stdin);
        return;
    }
    if (!strchr(out, '\n')) {
        while ((ch = getchar()) != '\n' && ch != EOF) {}
    }
    out[strcspn(out, "\r\n")] = '\0';
}

static int readInt(const char *prompt, int min, int max) {
    char buf[100], *end;
    long value;
    for (;;) {
        readLine(prompt, buf, sizeof(buf));
        value = strtol(buf, &end, 10);
        while (isspace((unsigned char)*end)) end++;
        if (buf[0] && *end == '\0' && value >= min && value <= max)
            return (int)value;
        printf("Enter a whole number from %d to %d.\n", min, max);
    }
}

static double readDouble(const char *prompt, double min, double max) {
    char buf[100], *end;
    double value;
    for (;;) {
        readLine(prompt, buf, sizeof(buf));
        value = strtod(buf, &end);
        while (isspace((unsigned char)*end)) end++;
        if (buf[0] && *end == '\0' && value >= min && value <= max)
            return value;
        printf("Enter a number from %.2f to %.2f.\n", min, max);
    }
}

static void copyText(char *dst, size_t size, const char *src) {
    if (size == 0) return;
    snprintf(dst, size, "%s", src);
}

static void pauseScreen(void) {
    char temp[8];
    readLine("\nPress Enter to continue...", temp, sizeof(temp));
}

/* ---------- Linked-list lookup and IDs ---------- */

static Application *findApplicationByStudent(int studentId) {
    Application *p;
    for (p = applications; p; p = p->next)
        if (p->studentId == studentId) return p;
    return NULL;
}

static int nextStudentId(void) {
    int maxId = 1000;
    Student *p;
    for (p = students; p; p = p->next)
        if (p->id > maxId) maxId = p->id;
    return maxId + 1;
}

static int nextCollegeId(void) {
    int maxId = 0;
    College *p;
    for (p = colleges; p; p = p->next)
        if (p->id > maxId) maxId = p->id;
    return maxId + 1;
}

static int nextApplicationId(void) {
    int maxId = 2000;
    Application *p;
    for (p = applications; p; p = p->next)
        if (p->id > maxId) maxId = p->id;
    return maxId + 1;
}

/* ---------- Hash table: email -> student ---------- */

static unsigned long emailHash(const char *s) {
    unsigned long hash = 5381;
    while (*s) hash = ((hash << 5) + hash) + (unsigned char)tolower((unsigned char)*s++);
    return hash % EMAIL_BUCKETS;
}

static Student *findStudentByEmail(const char *email) {
    EmailEntry *p;
    unsigned long bucket = emailHash(email);
    for (p = emailTable[bucket]; p; p = p->next)
        if (strcmp(p->email, email) == 0) return p->student;
    return NULL;
}

static int indexStudentEmail(Student *s) {
    unsigned long bucket = emailHash(s->email);
    EmailEntry *entry = (EmailEntry *)malloc(sizeof(*entry));
    if (!entry) return 0;
    copyText(entry->email, sizeof(entry->email), s->email);
    entry->student = s;
    entry->next = emailTable[bucket];
    emailTable[bucket] = entry;
    return 1;
}

static void rebuildEmailTable(void) {
    Student *p;
    EmailEntry *e, *next;
    int i;
    for (i = 0; i < EMAIL_BUCKETS; i++) {
        e = emailTable[i];
        while (e) {
            next = e->next;
            free(e);
            e = next;
        }
        emailTable[i] = NULL;
    }
    for (p = students; p; p = p->next) indexStudentEmail(p);
}

/* ---------- Binary search tree: college ID index ---------- */

static CollegeNode *bstInsert(CollegeNode *root, College *c) {
    if (!root) {
        CollegeNode *node = (CollegeNode *)malloc(sizeof(*node));
        if (!node) {
            printf("Warning: not enough memory to index a college.\n");
            return NULL;
        }
        node->college = c;
        node->left = node->right = NULL;
        return node;
    }
    if (c->id < root->college->id)
        root->left = bstInsert(root->left, c);
    else if (c->id > root->college->id)
        root->right = bstInsert(root->right, c);
    else
        root->college = c;
    return root;
}

static College *bstSearch(CollegeNode *root, int id) {
    while (root) {
        if (id == root->college->id) return root->college;
        root = id < root->college->id ? root->left : root->right;
    }
    return NULL;
}

static void freeCollegeTree(CollegeNode *root) {
    if (!root) return;
    freeCollegeTree(root->left);
    freeCollegeTree(root->right);
    free(root);
}

static void rebuildCollegeTree(void) {
    College *p;
    freeCollegeTree(collegeRoot);
    collegeRoot = NULL;
    for (p = colleges; p; p = p->next)
        collegeRoot = bstInsert(collegeRoot, p);
}

static void saveAll(void) {
    FILE *f;
    Student *s;
    College *c;
    Application *a;
    Preference *pr;

    f = fopen(DATA_STUDENTS, "w");
    if (f) {
        for (s = students; s; s = s->next)
            fprintf(f, "%d|%s|%s|%s|%s|%s|%s|%.2f|%.2f|%.2f|%s|%.2f|%.2f|%.2f|%d\n",
                s->id, s->name, s->email, s->password, s->phone, s->dob,
                s->address, s->physics, s->chemistry, s->mathematics,
                s->optionalSubject, s->optionalMark, s->hscTotal,
                s->cutoff, s->registered);
        fclose(f);
    } else printf("Warning: could not save student file.\n");

    f = fopen(DATA_COLLEGES, "w");
    if (f) {
        for (c = colleges; c; c = c->next)
            fprintf(f, "%d|%s|%s|%s|%.2f|%d|%d\n",
                c->id, c->name, c->location, c->branch, c->cutoff, c->seats, c->active);
        fclose(f);
    } else printf("Warning: could not save college file.\n");

    f = fopen(DATA_APPS, "w");
    if (f) {
        for (a = applications; a; a = a->next) {
            fprintf(f, "%d|%d|%d|%d|%s|", a->id, a->studentId,
                    a->submitted, a->allottedCollegeId, a->status);
            for (pr = a->choices; pr; pr = pr->next)
                fprintf(f, "%d,", pr->collegeId);
            fputc('\n', f);
        }
        fclose(f);
    } else printf("Warning: could not save application file.\n");
}

static void appendPreference(Application *a, int collegeId) {
    Preference *node = (Preference *)malloc(sizeof(*node));
    Preference *p;
    if (!node) return;
    node->collegeId = collegeId;
    node->next = NULL;
    if (!a->choices) a->choices = node;
    else {
        for (p = a->choices; p->next; p = p->next) {}
        p->next = node;
    }
    a->choiceCount++;
}

static void loadAll(void) {
    FILE *f;
    char line[1200], *tok;
    Student *s;
    College *c;
    Application *a;
    int field;

    f = fopen(DATA_STUDENTS, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            char *fields[20];
            int count = 0;
            char *start = line;
            char *cursor;
            /* Split while preserving empty fields; strtok would collapse them. */
            for (cursor = line; ; cursor++) {
                if (*cursor == '|' || *cursor == '\n' || *cursor == '\r' || *cursor == '\0') {
                    char delimiter = *cursor;
                    *cursor = '\0';
                    if (count < 20) fields[count++] = start;
                    if (delimiter == '\0' || delimiter == '\n' || delimiter == '\r') break;
                    start = cursor + 1;
                }
            }

            s = (Student *)calloc(1, sizeof(*s));
            if (!s) break;

            if (count >= 15) {
                /* Current format stores HSC subject marks and the calculated cutoff. */
                s->id = atoi(fields[0]);
                copyText(s->name, sizeof(s->name), fields[1]);
                copyText(s->email, sizeof(s->email), fields[2]);
                copyText(s->password, sizeof(s->password), fields[3]);
                copyText(s->phone, sizeof(s->phone), fields[4]);
                copyText(s->dob, sizeof(s->dob), fields[5]);
                copyText(s->address, sizeof(s->address), fields[6]);
                s->physics = atof(fields[7]);
                s->chemistry = atof(fields[8]);
                s->mathematics = atof(fields[9]);
                copyText(s->optionalSubject, sizeof(s->optionalSubject), fields[10]);
                s->optionalMark = atof(fields[11]);
                s->hscTotal = atof(fields[12]);
                s->cutoff = atof(fields[13]);
                s->registered = atoi(fields[14]);
            } else if (count >= 10) {
                /* Migrate older records: old format had an obsolete field at index 6,
                   address at field 7, cutoff at field 8, registered at field 9. */
                s->id = atoi(fields[0]);
                copyText(s->name, sizeof(s->name), fields[1]);
                copyText(s->email, sizeof(s->email), fields[2]);
                copyText(s->password, sizeof(s->password), fields[3]);
                copyText(s->phone, sizeof(s->phone), fields[4]);
                copyText(s->dob, sizeof(s->dob), fields[5]);
                copyText(s->address, sizeof(s->address), fields[7]);
                s->cutoff = atof(fields[8]);
                s->registered = atoi(fields[9]);
                /* Old records did not store individual marks. Their cutoff is
                   preserved, but those marks remain zero until profile edit. */
                copyText(s->optionalSubject, sizeof(s->optionalSubject), "CS");
            } else {
                free(s);
                continue;
            }
            s->next = students;
            students = s;
        }
        fclose(f);
    }
    f = fopen(DATA_COLLEGES, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            c = (College *)calloc(1, sizeof(*c));
            if (!c) break;
            field = 0;
            tok = strtok(line, "|\r\n");
            while (tok) {
                switch (field++) {
                    case 0: c->id = atoi(tok); break;
                    case 1: copyText(c->name, sizeof(c->name), tok); break;
                    case 2: copyText(c->location, sizeof(c->location), tok); break;
                    case 3: copyText(c->branch, sizeof(c->branch), tok); break;
                    case 4: c->cutoff = atof(tok); break;
                    case 5: c->seats = atoi(tok); break;
                    case 6: c->active = atoi(tok); break;
                }
                tok = strtok(NULL, "|\r\n");
            }
            if (field >= 7) {
                c->next = colleges;
                colleges = c;
            } else free(c);
        }
        fclose(f);
    }

    f = fopen(DATA_APPS, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            char *choiceText;
            a = (Application *)calloc(1, sizeof(*a));
            if (!a) break;
            tok = strtok(line, "|\r\n");
            if (!tok) { free(a); continue; }
            a->id = atoi(tok);
            tok = strtok(NULL, "|\r\n"); if (!tok) { free(a); continue; } a->studentId = atoi(tok);
            tok = strtok(NULL, "|\r\n"); if (!tok) { free(a); continue; } a->submitted = atoi(tok);
            tok = strtok(NULL, "|\r\n"); if (!tok) { free(a); continue; } a->allottedCollegeId = atoi(tok);
            tok = strtok(NULL, "|\r\n"); if (!tok) { free(a); continue; } copyText(a->status, sizeof(a->status), tok);
            choiceText = strtok(NULL, "|\r\n");
            if (choiceText) {
                char *end = choiceText, *comma;
                while (*end) {
                    long id = strtol(end, &comma, 10);
                    if (comma == end) break;
                    if (id > 0) appendPreference(a, (int)id);
                    if (*comma == ',') end = comma + 1;
                    else break;
                }
            }
            a->next = applications;
            applications = a;
        }
        fclose(f);
    }

    rebuildEmailTable();
    rebuildCollegeTree();
}

/* ---------- Student and college operations ---------- */

static void listColleges(void) {
    College *c;
    int count = 0;
    printf("\n%-5s %-28s %-18s %-24s %-8s %-6s\n",
           "ID", "College", "Location", "Branch", "Cutoff", "Seats");
    printf("-----------------------------------------------------------------------------------------------\n");
    for (c = colleges; c; c = c->next) {
        if (!c->active) continue;
        printf("%-5d %-28.28s %-18.18s %-24.24s %-8.2f %-6d\n",
               c->id, c->name, c->location, c->branch, c->cutoff, c->seats);
        count++;
    }
    if (!count) printf("No active colleges configured. Log in as admin to add demo colleges.\n");
}

static void collectAcademicMarks(Student *s) {
    int subjectChoice;
    double minimumTotal;

    puts("\n--- HSC (+2) ACADEMIC DETAILS ---");
    puts("Enter marks obtained in the three engineering-cutoff subjects (each out of 100).");
    s->physics = readDouble("Physics mark (0-100): ", 0, 100);
    s->chemistry = readDouble("Chemistry mark (0-100): ", 0, 100);
    s->mathematics = readDouble("Mathematics mark (0-100): ", 0, 100);

    puts("Optional subject:");
    puts("1. Computer Science (CS)");
    puts("2. Biology (BIO)");
    subjectChoice = readInt("Select optional subject: ", 1, 2);
    copyText(s->optionalSubject, sizeof(s->optionalSubject),
             subjectChoice == 1 ? "CS" : "BIO");
    s->optionalMark = readDouble("Optional subject mark (0-100): ", 0, 100);

    minimumTotal = s->physics + s->chemistry + s->mathematics + s->optionalMark;
    printf("Enter total HSC (+2) marks obtained (out of 600; must be at least %.2f for the marks entered).\n",
           minimumTotal);
    s->hscTotal = readDouble("HSC total marks (0-600): ", minimumTotal, 600);

    /* TNEA-style engineering cutoff out of 200. Optional subject and overall
       HSC total are recorded but do not enter this formula. */
    s->cutoff = s->mathematics + (s->physics / 2.0) + (s->chemistry / 2.0);
    printf("Calculated engineering cutoff: %.2f / 200\n", s->cutoff);
}

static void registerStudent(void) {
    Student *s;
    char confirm[PASS_LEN];
    s = (Student *)calloc(1, sizeof(*s));
    if (!s) { puts("Memory allocation failed."); return; }

    readLine("Full name (no | character): ", s->name, sizeof(s->name));
    if (!s->name[0] || strchr(s->name, '|')) { puts("Invalid name."); free(s); return; }

    readLine("Email: ", s->email, sizeof(s->email));
    if (!strchr(s->email, '@') || strchr(s->email, '|') || findStudentByEmail(s->email)) {
        puts("Invalid or already registered email."); free(s); return;
    }

    readLine("Create password: ", s->password, sizeof(s->password));
    readLine("Confirm password: ", confirm, sizeof(confirm));
    if (strlen(s->password) < 4 || strcmp(s->password, confirm) != 0 || strchr(s->password, '|')) {
        puts("Passwords must match, contain at least 4 characters, and not contain |.");
        free(s); return;
    }

    readLine("Phone: ", s->phone, sizeof(s->phone));
    readLine("DOB (DD-MM-YYYY): ", s->dob, sizeof(s->dob));
    readLine("Address (no | character): ", s->address, sizeof(s->address));
    if (strchr(s->address, '|')) { puts("Address cannot contain |."); free(s); return; }
    collectAcademicMarks(s);
    s->id = nextStudentId();
    s->registered = 1;

    s->next = students;
    students = s;
    if (!indexStudentEmail(s)) puts("Warning: email index allocation failed; restart to rebuild index.");
    saveAll();
    printf("Registration successful. Student ID: %d\n", s->id);
}

static Student *studentLogin(void) {
    char email[EMAIL_LEN], password[PASS_LEN];
    int tries;
    for (tries = 0; tries < 3; tries++) {
        readLine("Email: ", email, sizeof(email));
        readLine("Password: ", password, sizeof(password));
        Student *s = findStudentByEmail(email);
        if (s && strcmp(s->password, password) == 0) return s;
        puts("Incorrect email or password.");
    }
    return NULL;
}

static void viewProfile(Student *s) {
    printf("\nID: %d\nName: %s\nEmail: %s\nPhone: %s\nDOB: %s\nAddress: %s\n"
           "Physics: %.2f/100\nChemistry: %.2f/100\nMathematics: %.2f/100\n"
           "Optional subject: %s (%.2f/100)\nHSC total: %.2f/600\nEngineering cutoff: %.2f/200\n",
        s->id, s->name, s->email, s->phone, s->dob, s->address,
        s->physics, s->chemistry, s->mathematics, s->optionalSubject,
        s->optionalMark, s->hscTotal, s->cutoff);
}

static void editProfile(Student *s) {
    char buf[TEXT_LEN];
    readLine("Name (blank keeps current): ", buf, sizeof(buf));
    if (*buf && !strchr(buf, '|')) copyText(s->name, sizeof(s->name), buf);
    readLine("Phone (blank keeps current): ", buf, sizeof(buf));
    if (*buf) copyText(s->phone, sizeof(s->phone), buf);
    readLine("DOB (blank keeps current): ", buf, sizeof(buf));
    if (*buf) copyText(s->dob, sizeof(s->dob), buf);
    readLine("Address (blank keeps current): ", buf, sizeof(buf));
    if (*buf && !strchr(buf, '|')) copyText(s->address, sizeof(s->address), buf);
    collectAcademicMarks(s);
    saveAll();
    puts("Profile updated.");
}

static int hasPreference(Application *a, int collegeId) {
    Preference *p;
    for (p = a->choices; p; p = p->next)
        if (p->collegeId == collegeId) return 1;
    return 0;
}

static void clearPreferences(Application *a) {
    Preference *p = a->choices, *next;
    while (p) { next = p->next; free(p); p = next; }
    a->choices = NULL;
    a->choiceCount = 0;
}

static void fillChoices(Student *s) {
    Application *a = findApplicationByStudent(s->id);
    int id, count = 0;
    Preference *p;
    College *c;

    if (a && a->submitted) {
        puts("Submitted choices are locked in this demo.");
        return;
    }
    if (!a) {
        a = (Application *)calloc(1, sizeof(*a));
        if (!a) { puts("Memory allocation failed."); return; }
        a->id = nextApplicationId();
        a->studentId = s->id;
        copyText(a->status, sizeof(a->status), "Draft");
        a->next = applications;
        applications = a;
    } else clearPreferences(a);

    listColleges();
    puts("\nEnter college IDs in preference order; enter 0 to finish.");
    while (count < 10) {
        id = readInt("College ID (0 to finish): ", 0, 1000000);
        if (id == 0) break;
        c = bstSearch(collegeRoot, id);
        if (!c || !c->active) { puts("College not found or inactive."); continue; }
        if (hasPreference(a, id)) { puts("Duplicate preference."); continue; }
        appendPreference(a, id);
        count++;
    }
    if (a->choiceCount == 0) {
        puts("No choices entered; application remains a draft.");
        saveAll();
        return;
    }

    puts("\nYour preferences:");
    count = 0;
    for (p = a->choices; p; p = p->next) {
        c = bstSearch(collegeRoot, p->collegeId);
        if (c) printf("%d. %s - %s\n", ++count, c->name, c->branch);
    }
    if (readInt("Submit? (1=yes, 2=save draft): ", 1, 2) == 1) {
        a->submitted = 1;
        copyText(a->status, sizeof(a->status), "Submitted");
    } else {
        a->submitted = 0;
        copyText(a->status, sizeof(a->status), "Draft");
    }
    saveAll();
    printf("Application ID: %d; status: %s\n", a->id, a->status);
}

static void viewApplication(int studentId) {
    Application *a = findApplicationByStudent(studentId);
    Preference *p;
    College *c;
    int i = 1;
    if (!a) { puts("No application found."); return; }
    printf("Application ID: %d\nStatus: %s\nSubmitted: %s\n",
        a->id, a->status, a->submitted ? "Yes" : "No");
    for (p = a->choices; p; p = p->next) {
        c = bstSearch(collegeRoot, p->collegeId);
        if (c) printf("%d. %s - %s\n", i++, c->name, c->branch);
        else printf("%d. College ID %d (record missing)\n", i++, p->collegeId);
    }
    if (a->allottedCollegeId && (c = bstSearch(collegeRoot, a->allottedCollegeId)))
        printf("Allotted: %s - %s\n", c->name, c->branch);
    else puts("Allotment: pending / not allotted");
}

static void changePassword(Student *s) {
    char oldPass[PASS_LEN], newPass[PASS_LEN], confirm[PASS_LEN];
    readLine("Current password: ", oldPass, sizeof(oldPass));
    if (strcmp(oldPass, s->password)) { puts("Incorrect password."); return; }
    readLine("New password: ", newPass, sizeof(newPass));
    readLine("Confirm new password: ", confirm, sizeof(confirm));
    if (strlen(newPass) < 4 || strchr(newPass, '|') || strcmp(newPass, confirm)) {
        puts("Passwords must match, be at least 4 characters, and not contain |."); return;
    }
    copyText(s->password, sizeof(s->password), newPass);
    saveAll();
    puts("Password changed.");
}

/* ---------- Max heap: cutoff priority queue ---------- */

static void heapPush(HeapItem heap[], int *size, Student *s) {
    int i = (*size)++;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap[parent].student->cutoff >= s->cutoff) break;
        heap[i] = heap[parent];
        i = parent;
    }
    heap[i].student = s;
}

static Student *heapPop(HeapItem heap[], int *size) {
    Student *result, *last;
    int i = 0;
    if (*size == 0) return NULL;
    result = heap[0].student;
    last = heap[--(*size)].student;
    if (*size == 0) return result;

    while (2 * i + 1 < *size) {
        int child = 2 * i + 1;
        if (child + 1 < *size &&
            heap[child + 1].student->cutoff > heap[child].student->cutoff)
            child++;
        if (last->cutoff >= heap[child].student->cutoff) break;
        heap[i] = heap[child];
        i = child;
    }
    heap[i].student = last;
    return result;
}

static void runAllotment(void) {
    Student *s;
    Application *a;
    Preference *p;
    College *c;
    HeapItem *heap;
    int size = 0, capacity = 0, i;
    Student **eligible = NULL;

    for (s = students; s; s = s->next) capacity++;
    if (!capacity) { puts("No students registered."); return; }

    heap = (HeapItem *)malloc(sizeof(*heap) * (size_t)capacity);
    eligible = (Student **)malloc(sizeof(*eligible) * (size_t)capacity);
    if (!heap || !eligible) {
        free(heap); free(eligible); puts("Not enough memory for allotment."); return;
    }

    /* Build max heap from submitted applications only. */
    for (s = students; s; s = s->next) {
        a = findApplicationByStudent(s->id);
        if (a && a->submitted) heapPush(heap, &size, s);
    }

    if (!size) { puts("No submitted applications to process."); free(heap); free(eligible); return; }

    /* Pop priority queue in descending cutoff order. */
    i = 0;
    while (size > 0) eligible[i++] = heapPop(heap, &size);

    for (int k = 0; k < i; k++) {
        s = eligible[k];
        a = findApplicationByStudent(s->id);
        a->allottedCollegeId = 0;
        copyText(a->status, sizeof(a->status), "Not Allotted");

        for (p = a->choices; p; p = p->next) {
            c = bstSearch(collegeRoot, p->collegeId);
            if (c && c->active && c->seats > 0 && s->cutoff >= c->cutoff) {
                a->allottedCollegeId = c->id;
                copyText(a->status, sizeof(a->status), "Allotted");
                c->seats--;
                break;
            }
        }
    }
    saveAll();
    puts("Demo allotment completed using a max heap priority queue.");
    puts("Students were processed from highest to lowest demo cutoff.");
    puts("This is not the official TNEA counselling algorithm.");
    free(heap);
    free(eligible);
}

/* ---------- Administrator operations ---------- */

static void addCollege(void) {
    College *c = (College *)calloc(1, sizeof(*c));
    if (!c) { puts("Memory allocation failed."); return; }
    readLine("College name (no | character): ", c->name, sizeof(c->name));
    readLine("Location/district: ", c->location, sizeof(c->location));
    readLine("Branch/course: ", c->branch, sizeof(c->branch));
    if (strchr(c->name, '|') || strchr(c->location, '|') || strchr(c->branch, '|')) {
        puts("Fields cannot contain |."); free(c); return;
    }
    c->cutoff = readDouble("Demo cutoff (0-200): ", 0, 200);
    c->seats = readInt("Available seats: ", 1, 100000);
    c->id = nextCollegeId();
    c->active = 1;
    c->next = colleges;
    colleges = c;
    collegeRoot = bstInsert(collegeRoot, c);
    saveAll();
    printf("College added. ID: %d\n", c->id);
}

static void toggleCollege(void) {
    int id;
    College *c;
    listColleges();
    id = readInt("College ID to activate/deactivate: ", 1, 1000000);
    c = bstSearch(collegeRoot, id);
    if (!c) { puts("College not found."); return; }
    c->active = !c->active;
    saveAll();
    printf("%s is now %s.\n", c->name, c->active ? "ACTIVE" : "INACTIVE");
}

static void adminListStudents(void) {
    Student *s;
    int count = 0;
    printf("\n%-8s %-22s %-28s %-9s %-9s %-9s %-10s\n",
           "ID", "Name", "Email", "Physics", "Chemistry", "Maths", "Cutoff");
    for (s = students; s; s = s->next) {
        printf("%-8d %-22.22s %-28.28s %-9.2f %-9.2f %-9.2f %-10.2f\n",
               s->id, s->name, s->email, s->physics, s->chemistry,
               s->mathematics, s->cutoff);
        count++;
    }
    printf("Total students: %d\n", count);
}

static void adminListApplications(void) {
    Application *a;
    int count = 0;
    printf("\n%-12s %-12s %-16s %-10s %-12s\n",
        "Application", "Student", "Status", "Choices", "Allotted ID");
    for (a = applications; a; a = a->next) {
        printf("%-12d %-12d %-16s %-10d %-12d\n",
            a->id, a->studentId, a->status, a->choiceCount, a->allottedCollegeId);
        count++;
    }
    printf("Total applications: %d\n", count);
}

static void adminMenu(void) {
    char password[PASS_LEN];
    int choice;
    readLine("Admin password: ", password, sizeof(password));
    if (strcmp(password, ADMIN_PASSWORD)) { puts("Access denied."); return; }

    do {
        puts("\n========== TNEA ADMIN PANEL ==========");
        puts("1. Add college/course");
        puts("2. List colleges/courses");
        puts("3. Activate/deactivate college");
        puts("4. List students");
        puts("5. List applications");
        puts("6. Run max-heap demo allotment");
        puts("0. Logout");
        choice = readInt("Choice: ", 0, 6);
        switch (choice) {
            case 1: addCollege(); pauseScreen(); break;
            case 2: listColleges(); pauseScreen(); break;
            case 3: toggleCollege(); pauseScreen(); break;
            case 4: adminListStudents(); pauseScreen(); break;
            case 5: adminListApplications(); pauseScreen(); break;
            case 6: runAllotment(); pauseScreen(); break;
            case 0: puts("Admin logged out."); break;
        }
    } while (choice != 0);
}

static void studentMenu(Student *s) {
    int choice;
    do {
        printf("\n========== STUDENT DASHBOARD: %s ==========\n", s->name);
        puts("1. View profile");
        puts("2. Edit profile");
        puts("3. View colleges/courses");
        puts("4. Fill college preferences and submit");
        puts("5. View application/allotment");
        puts("6. Change password");
        puts("0. Logout");
        choice = readInt("Choice: ", 0, 6);
        switch (choice) {
            case 1: viewProfile(s); pauseScreen(); break;
            case 2: editProfile(s); pauseScreen(); break;
            case 3: listColleges(); pauseScreen(); break;
            case 4: fillChoices(s); pauseScreen(); break;
            case 5: viewApplication(s->id); pauseScreen(); break;
            case 6: changePassword(s); pauseScreen(); break;
            case 0: puts("Logged out."); break;
        }
    } while (choice != 0);
}

/* ---------- Cleanup ---------- */

static void cleanup(void) {
    Student *s = students, *sn;
    College *c = colleges, *cn;
    Application *a = applications, *an;
    Preference *p, *pn;
    EmailEntry *e, *en;
    int i;

    while (s) { sn = s->next; free(s); s = sn; }
    while (c) { cn = c->next; free(c); c = cn; }
    while (a) {
        an = a->next;
        p = a->choices;
        while (p) { pn = p->next; free(p); p = pn; }
        free(a); a = an;
    }
    for (i = 0; i < EMAIL_BUCKETS; i++) {
        e = emailTable[i];
        while (e) { en = e->next; free(e); e = en; }
    }
    freeCollegeTree(collegeRoot);
}

static void useExecutableDirectory(const char *argv0) {
    char path[1024];
    char *lastSlash, *lastBackslash, *separator;

    if (!argv0 || !*argv0 || strlen(argv0) >= sizeof(path)) return;
    copyText(path, sizeof(path), argv0);
    lastSlash = strrchr(path, '/');
    lastBackslash = strrchr(path, '\\');
    separator = lastSlash;
    if (!separator || (lastBackslash && lastBackslash > separator))
        separator = lastBackslash;

    /* If launched with a path, keep data files beside the executable.
       If argv[0] is only a filename, the current directory is already used. */
    if (separator) {
        *separator = '\0';
        if (path[0] != '\0' && CHANGE_DIR(path) != 0)
            fprintf(stderr, "Warning: cannot switch to executable directory '%s'. Data files will use the current directory.\n", path);
    }
}

int main(int argc, char *argv[]) {
    int choice;
    Student *s;
    char cwd[1024];
    (void)argc;
    useExecutableDirectory(argv[0]);
    loadAll();

    // if (GETCWD(cwd, sizeof(cwd)) != NULL)
    //     printf("Data directory: %s\n", cwd);
    // else
    //     puts("Data directory: current working directory (path unavailable)");
    // printf("Loaded records: students=%s, colleges=%s, applications=%s\n",
    //        students ? "yes" : "none", colleges ? "yes" : "none",
    //        applications ? "yes" : "none");

    puts("==================================================");
    puts("   TNEA MANAGEMENT SYSTEM");
    puts("==================================================");
    // puts("Structures demonstrated: linked lists, hash table, BST, max heap.");
    // puts("Educational prototype; not the official TNEA portal.");

    do {
        puts("\n================ MAIN MENU ================");
        puts("1. Student registration");
        puts("2. Student login");
        puts("3. Administrator login");
        puts("4. View available colleges/courses");
        // puts("5. About data structures");
        puts("0. Exit");
        choice = readInt("Choice: ", 0, 5);
        switch (choice) {
            case 1: registerStudent(); pauseScreen(); break;
            case 2:
                s = studentLogin();
                if (s) { printf("Welcome, %s!\n", s->name); studentMenu(s); }
                else puts("Login failed.");
                pauseScreen();
                break;
            case 3: adminMenu(); pauseScreen(); break;
            case 4: listColleges(); pauseScreen(); break;
            case 0: saveAll(); puts("Data saved. Goodbye."); break;
        }
    } while (choice != 0);

    cleanup();
    return 0;
}
