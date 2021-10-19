#include<iostream>
#include<vector>
#include<string>
#include<stack>
#include<iomanip>
#include<regex>

#include<termios.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/ioctl.h>
#include<errno.h>
#include<signal.h>
#include<stdlib.h>
#include<string.h>
#include<dirent.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<ftw.h>
#include<pwd.h>

using namespace std;

#define TRUE 1
#define FALSE 0

#define NORMAL_MODE 0
#define COMMAND_MODE 1

#define MOVE(x, y) cout<<"\033["<<x<<";"<<y<<"H"<<flush
#define CLEAR() cout<<"\033[H"<<"\033[J"<<flush
#define SAVE_CURSOR() cout<<"\033[s"<<flush
#define RESTORE_CURSOR() cout<<"\033[u"<<flush
#define CLEAR_LINE() cout<<"\033[K"<<flush
#define MOVE_CURSOR_UP() cout<<"\033[1A"<<flush;
#define MOVE_CURSOR_DOWN() cout<<"\033[1B"<<flush;
#define FLUSH() cout<<flush
#define SET_COLOR(x) cout<<"\033[38;5;"+to_string(x)+"m";
#define SET_BG_COLOR(x) cout<<"\033[48;5;"+to_string(x)+"m";

#define FILE_NAME(abs_path) abs_path.substr(abs_path.find_last_of("/")+1)
#define IS_RELATIVE(path) path[0] != '/'
#define BASE_NAME(file_name) file_name.substr(0, file_name.find_last_of("."))
#define EXT(file_name) file_name.substr(file_name.find_last_of(".")+1)

struct cursor_pos {
    int i, j;
};

struct entry {
    string name;
    string size;
    string owner;
    string permission;
    string last_mod;
};

bool comp_entry(entry &e1, entry &e2) {
    if(e1.name.size()>0 && e2.name.size()>0) {
        if(e1.name[0] == '.' && e2.name[0] == '.')
            return e1.name.substr(1) < e2.name.substr(1);
        else if(e1.name[0] == '.')
            return true;
        else if(e2.name[0] == '.')
            return false;
        else
            return e1.name < e2.name;
    }
    return false;
}

/*
    Global Configuration Variables
*/
struct termios tty_config_old;
struct termios tty_config;
struct winsize tty_ws;
const char *tty = "/dev/tty";
/* file descriptor for tty */
int fd;


/*
    Global Variables specific to application
*/
/* system root */
string root = "/";
/* user home */
string home;
/* application home */
string app_home;
/* saved cursor position */
cursor_pos saved_cpos;
/* status bar start position */
cursor_pos st_start;
/* header start position */
cursor_pos h_start;
/* index of first entry to be displayed */
int st;
/* top row number of display area */
int top;
/* bottom row number of display area */
int bottom;
/* top offset for display area */
int top_offset;
/* bottom offset for display area */
int bottom_offset;
/* current working directory */
string cdir;
/* array to store enteries of files in current dir */
vector<entry> enteries;
/* current mode */
int mode;
/* current status */
string status;
/* header */
string header;
/* left stack */
stack<string> lstack;
/* right stack */
stack<string> rstack;
/* index of currently selected entry */
int selected_entry;
/* stores inp in command mode */
string inp;

// /* total size */
// long long total;


string join(string path1, string path2) {
    if(path2.size()>0 && path2[0] == '/')
        return path2;
    else if(path1.size()>0 && path1[path1.size()-1] == '/')
        return path1 + path2;
    else
        return path1 + "/" + path2;
}

bool exists(string abs_path) {
    struct stat fst;
    return stat(abs_path.c_str(), &fst) == 0;
}

bool is_dir(string abs_path) {
    struct stat fst;
    if(stat(abs_path.c_str(), &fst) == 0)
        return S_ISDIR(fst.st_mode);
    return false;
}

string expand_tilde(string path) {
    if(path.size() > 0 && path[0] == '~') {
        if(path.size()>1 && path[1] == '/')
            return join(home, path.substr(2));
        else if(path.size() == 1)
            return home;
    }
    return path;
}

bool is_empty_dir(string abs_dir_path) {
    if(!is_dir(abs_dir_path))
        return false;
    
    DIR *dir = opendir(abs_dir_path.c_str());
    int count = 0;
    dirent *ent;
    while((ent = readdir(dir)) != NULL) {
        ++count;
        if(count > 2) break;
    }
    closedir(dir);
    return count <= 2;
}
/*
    functions dealing with content rendering
*/
void render_status() {
    SAVE_CURSOR();
    MOVE(st_start.i, st_start.j);
    CLEAR_LINE();
    SET_COLOR(202);
    cout<<status;
    RESTORE_CURSOR();
}

void render_input() {
    SAVE_CURSOR();
    MOVE(bottom - (bottom_offset-1), 1);
    CLEAR_LINE();
    // SET_COLOR(202);
    cout<<inp;
    RESTORE_CURSOR();
}

void set_status(string sts, bool render = false) {
    status = sts;
    if(render)
        render_status();
}

void render_header() {
    SAVE_CURSOR();
    MOVE(h_start.i, h_start.j);
    CLEAR_LINE();
    SET_COLOR(111);
    cout<<header;
    RESTORE_CURSOR();
}

void set_header(string h, bool render = false) {
    header = h;
    if(render)
        render_header();
}

void save_cursor(int i, int j) {
    saved_cpos.i = i;
    saved_cpos.j = j;
}

void render() {
    /* clear screen */
    CLEAR();

    /* render header */
    render_header();

    MOVE(top, 1);
    vector<int> max_col_widths(5);
    int end = st + min(bottom - top + 1, (int)enteries.size());
    for(int i=st; i<end; ++i) {
        max_col_widths[0] = max(max_col_widths[0], (int)enteries[i].name.size());
        max_col_widths[1] = max(max_col_widths[1], (int)enteries[i].size.size());
        max_col_widths[2] = max(max_col_widths[2], (int)enteries[i].owner.size());
        max_col_widths[3] = max(max_col_widths[3], (int)enteries[i].permission.size());
        max_col_widths[4] = max(max_col_widths[4], (int)enteries[i].last_mod.size());
    }
    /* render content */
    for(int i=st; i<end; ++i) {
        // cout<<i<<"\n";
        cout<<left<<setw(max_col_widths[0])<<enteries[i].name<<"  ";
        cout<<left<<setw(max_col_widths[1])<<enteries[i].size<<"  ";
        cout<<left<<setw(1)<<"B"<<" ";
        cout<<left<<setw(max_col_widths[2])<<enteries[i].owner<<"  ";
        cout<<left<<setw(max_col_widths[3])<<enteries[i].permission<<"  ";
        cout<<left<<setw(max_col_widths[4])<<enteries[i].last_mod;
        if(i<end-1) cout<<"\n";
    }
    cout<<flush;

    if(mode == NORMAL_MODE) {
        MOVE(top + selected_entry-st, 1);
    } else {
        MOVE(tty_ws.ws_row - (bottom_offset-1), 1);
        cout<<inp;
    }


    /* print status */
    render_status();
}

// int sum(const char *path, const struct stat *sb, int typeflag, struct FTW *ftwflag) {
//     total += sb->st_size;
//     return 0;
// }

/* store all info about files of current 
    directory in enteries array */
void list() {
    DIR *dir = opendir(cdir.c_str());
    if(dir == NULL) {
        return;
    }
    enteries.clear();

    dirent *ent;
    while((ent = readdir(dir)) != NULL) {
        string fname = ent->d_name;
        string abs_path = join(cdir, fname);
        
        struct stat st;
        stat(abs_path.c_str(), &st);

        entry e;
        e.name = fname;
        // if(S_ISDIR(st.st_mode) && fname != "..") {
        //     total = 0;
        //     nftw(abs_path.c_str(), sum, 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
        //     e.size = to_string(total);
        // }else {
        //     e.size = to_string(st.st_size);
        // }
        e.size = to_string(st.st_size);
        struct passwd *pd = getpwuid(st.st_uid);
        e.owner = pd->pw_name;
        /* extract file type and permisison info */
        if(S_ISREG(st.st_mode)) e.permission += "-";
        else if(S_ISBLK(st.st_mode)) e.permission += "b";
        else if(S_ISCHR(st.st_mode)) e.permission += "c";
        else if(S_ISDIR(st.st_mode)) e.permission += "d";
        else if(S_ISFIFO(st.st_mode)) e.permission += "p";
        else if(S_ISLNK(st.st_mode)) e.permission += "l";
        else if(S_ISSOCK(st.st_mode)) e.permission += "s";

        e.permission += (st.st_mode & S_IRUSR)?'r':'-';
        e.permission += (st.st_mode & S_IWUSR)?'w':'-';
        e.permission += (st.st_mode & S_IXUSR)?'x':'-';

        e.permission += (st.st_mode & S_IRGRP)?'r':'-';
        e.permission += (st.st_mode & S_IWGRP)?'w':'-';
        e.permission += (st.st_mode & S_IXGRP)?'x':'-';

        e.permission += (st.st_mode & S_IROTH)?'r':'-';
        e.permission += (st.st_mode & S_IWOTH)?'w':'-';
        e.permission += (st.st_mode & S_IXOTH)?'x':'-';
        /* extract last modified time */
        char buf[100];
        strftime(buf, sizeof buf, "%e %b %Y %H:%M:%S", localtime(&st.st_atim.tv_sec));
        e.last_mod += string(buf, strlen(buf));
        enteries.push_back(e);
    }
    closedir(dir);

    sort(enteries.begin(), enteries.end(), comp_entry);
}

/* Handle tty window resize */
void signal_win_resize(int signum) {
    if(ioctl(fd, TIOCGWINSZ, &tty_ws) < 0) {
        fprintf(stderr, "failed to obtain new size after window resize\n");
        exit(1);
    }
    /* re calculate header position here */
    h_start.i = 1;
    h_start.j = 1;

    /* re calculate status bar position here */
    st_start.i = tty_ws.ws_row;
    st_start.j = 1;

    /* recalculate display area */
    top = top_offset + 1;
    bottom = tty_ws.ws_row - bottom_offset;

    /* recalculate st */
    if(mode == NORMAL_MODE) {
        if((bottom - top + 1) >= enteries.size()) {
            st = 0;
        } else {
            int da_sz = (bottom - top + 1);
            int diff = (selected_entry - st + 1) - da_sz;
            if(diff > 0 && st + diff <= selected_entry)
                st += diff; 
        }
    }

    render();
}

void scroll_down() {
    if(selected_entry + 1 == enteries.size()) {
        set_status("Reached End", true);
        return;
    }
    ++selected_entry;
    int da_sz = bottom - top + 1;
    if((selected_entry - st + 1) > da_sz) {
        ++st;
        render();
    } else {
        MOVE_CURSOR_DOWN();
    }
}

void scroll_up() {
    if(selected_entry == 0) {
        set_status("At Top", true);
        return;
    }
    --selected_entry;
    if(selected_entry < st) {
        --st;
        render();
    } else {
        MOVE_CURSOR_UP();
    }
}

void update() {
    /* set header */
    set_header(cdir);

    /* get contents of cdir */
    list();

    /* set header poition */
    h_start.i = 1;
    h_start.j = 1;

    /* set status bar position */
    st_start.i = tty_ws.ws_row;
    st_start.j = 1;

    /* set display area */
    top = top_offset + 1;
    bottom = tty_ws.ws_row - bottom_offset;

    st = 0;
    selected_entry = 0;

    render();
}

void icanon(bool activate) {
    if(activate)
        tty_config.c_lflag |= ICANON;
    else
        tty_config.c_lflag &= ~ICANON;
    tcsetattr(fd, TCSANOW, &tty_config);
}

void echo(bool activate) {
    if(activate)
        tty_config.c_lflag |= ECHO;
    else
        tty_config.c_lflag &= ~ECHO;
    tcsetattr(fd, TCSANOW, &tty_config);
}

void activate_normal_mode() {
    icanon(false);
    echo(false);

    mode = NORMAL_MODE;

    // set_status("Normal Mode", true);
    update();
}
void activate_command_mode() {
    set_status("Command Mode", true);
    icanon(false);
    echo(false);
    mode = COMMAND_MODE;

    MOVE(tty_ws.ws_row - (bottom_offset-1), 1);
    CLEAR_LINE();
}


bool goto_dir(string abs_path) {
    if(!is_dir(abs_path))
        return false;
    if(FILE_NAME(abs_path) == ".")
        return true;
    if(FILE_NAME(abs_path) == "..") {
        abs_path = abs_path.substr(0, abs_path.size()-3);
        abs_path = abs_path.substr(0, abs_path.find_last_of('/'));
    }
    lstack.push(cdir);
    cdir = abs_path;
    while(!rstack.empty())
        rstack.pop();
    
    update();
    return true;
}

void go_back() {
    if(cdir == root) {
        set_status("At root", true);
        return;
    } else {
        /* compute new cdir */
        int pos = cdir.find_last_of('/');
        string new_cdir = cdir.substr(0, pos);
        if(new_cdir.size() == 0) new_cdir = "/";

        goto_dir(new_cdir);
    }
}

void go_left() {
    if(lstack.empty()) {
        set_status("At Left End", true);
        return;
    }

    rstack.push(cdir);
    cdir = lstack.top();
    lstack.pop();
    update();
}

void go_right() {
    if(rstack.empty()) {
        set_status("At Right End", true);
        return;
    }
    lstack.push(cdir);
    cdir = rstack.top();
    rstack.pop();
    update();
}

void open_file(string abs_path) {
    set_status("Trying to open", true);
    char *editor = getenv("EDITOR")?getenv("VISUAL"):NULL;
    char buf[20];
    strcpy(buf, "/bin/vim");
    editor = buf;

    char args[100];
    strcpy(args, abs_path.c_str());

    set_status(args, true);
    char *argv[] = {editor, args};
    pid_t pid = fork();
    /* TODO - check this its fragile */
    if(pid == 0) {
        int i = execv(editor, argv);
        set_status("child "+to_string(i), true);
    } else {
        int status;
        waitpid(pid, &status, 0);
        set_status("parent : "+to_string(status), true);
    }
}

void enter() {
    entry e = enteries[selected_entry];
    if(e.permission[0] == 'd' && e.name != ".") {
        if(e.name == "..") {
            go_back();
        } else {
            /* compute new cdir */
            string new_cdir = join(cdir, e.name);
            // if(cdir == "/") new_cdir = cdir + e.name;
            // else new_cdir = cdir+"/"+e.name;

            goto_dir(new_cdir);
        }
    } else if(e.permission[0] == '-') {
        open_file(join(cdir, e.name));
    }
}

/* 
    Command Mode functions
*/
/* Copy a file */
int copy_file(string abs_path, string dest_path) {
    if(is_dir(abs_path) || !exists(abs_path) || !is_dir(dest_path))
        return -1;
    string fname = FILE_NAME(abs_path);
    FILE *fp_src = fopen(abs_path.c_str(), "r");
    if(fp_src == NULL)
        return -1;
    
    /* obtain fstat */
    struct stat fst_src;
    fstat(fileno(fp_src), &fst_src);

    /* check for dest permissions */
    struct stat dest_stat;
    if(stat(dest_path.c_str(), &dest_stat) != 0 || 
        !(dest_stat.st_mode & S_IWUSR) || !(dest_stat.st_mode & S_IXUSR))
        return -2;

    /* find a unique name for copy file */
    struct stat temp;
    int count = 0;
    string temp_path = join(dest_path, fname);
    while(stat(temp_path.c_str(), &temp) == 0)
        temp_path = join(dest_path, BASE_NAME(fname) +
                     "(" + to_string(++count) + ")" + "." +
                     EXT(fname));
    
    string cpy_file_path = temp_path;

    set_status(fname + ":" + cpy_file_path, true);
    
    /* create new file at dest */
    FILE *fp_dst = fopen(cpy_file_path.c_str(), "w");

    /* copy contents */
    char buf[1024];
    int read_count;
    while((read_count = fread(buf, 1, 1024, fp_src)))
        fwrite(buf, 1, read_count, fp_dst);
    
    /* set ownership and permission as original */
    fchown(fileno(fp_dst), fst_src.st_uid, fst_src.st_gid);
    fchmod(fileno(fp_dst), fst_src.st_mode);

    fclose(fp_dst);
    fclose(fp_src);
    
    return 0;
}

/* Create new file */
int create_file(string fname, string abs_dest_path) {
    if(!exists(abs_dest_path))
        return -1;
    string abs_path = join(abs_dest_path, fname);
    FILE *fp = fopen(abs_path.c_str(), "a");
    if(fp == NULL)
        return -1;
    fclose(fp);
    return 0;
}

int create_dir(string dirname, string abs_dest_path) {
    return mkdir(join(abs_dest_path, dirname).c_str(), 0777);
}

string search_file(string abs_src_dir_path, string fname) {
    if(exists(join(abs_src_dir_path, fname)))
        return abs_src_dir_path;

    DIR *dir = opendir(abs_src_dir_path.c_str());
    if(dir == NULL)
        return "";
    dirent *ent;
    while((ent = readdir(dir)) != NULL) {
        string ent_name = ent->d_name;
        if(ent_name == "." || ent_name =="..")
            continue;
        string ent_path = join(abs_src_dir_path, ent_name);
        string search_res;
        if(is_dir(ent_path) && (search_res = search_file(ent_path, fname)) != "")
            return search_res;
    }
    closedir(dir);
    return "";
}

int copy_dir(string abs_srcdir_path, string abs_destdir_path) {
    if(!is_dir(abs_srcdir_path) || !is_dir(abs_destdir_path))
        return -11;
    
    /* open source dir */
    DIR *src = opendir(abs_srcdir_path.c_str());
    if(src == NULL)
        return -12;
    
    /* create empty destination dir */
    string dirname = FILE_NAME(abs_srcdir_path);
    set_status("creating "+join(abs_destdir_path, dirname), true);
    if(create_dir(dirname, abs_destdir_path) != 0)
        return -13;
    string abs_dest_path = join(abs_destdir_path, dirname);
    
    /* copy directory entires execept . and .. */
    dirent *ent;
    while((ent = readdir(src))!= NULL) {
        string fname = ent->d_name;
        if(fname == "." || fname == "..")
            continue;

        string abs_path = join(abs_srcdir_path, fname);

        set_status(abs_path, true);
        int status;
        if(!is_dir(abs_path))
            status = copy_file(abs_path, abs_dest_path);
        else
            status = copy_dir(abs_path, abs_dest_path);
        if(status != 0)
            return status;
    }

    closedir(src);

    /* modify the ownership and permission of created dir */
    struct stat fst_src;
    if(stat(abs_srcdir_path.c_str(), &fst_src) !=0)
        return -15;
    set_status(abs_dest_path, true);
    if(chown(abs_dest_path.c_str(), fst_src.st_uid, fst_src.st_gid) != 0)
        return -17;
    if(chmod(abs_dest_path.c_str(), fst_src.st_mode) != 0)
        return -18;

    return 0;
}


int delete_file(string abs_src_path) {
    if(is_dir(abs_src_path))
        return -1;
    return unlink(abs_src_path.c_str());
}

static int fn(const char *path, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    switch(typeflag) {
        case FTW_NS:
        case FTW_DNR: return -1;
        
        case FTW_F:
        case FTW_SL: return unlink(path);

        case FTW_DP: return rmdir(path);

        case FTW_D: return 0;
        
        default: return -1; 
    }
}

int delete_dir(string abs_src_path) {
    return nftw(abs_src_path.c_str(), fn, 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
}

/* 
    String processing functions
*/
string ltrim(const string &s) {
    size_t start = s.find_first_not_of(" \n\r\t\f\v");
    return (start == string::npos) ? "" : s.substr(start);
}
 
string rtrim(const string &s) {
    size_t end = s.find_last_not_of(" \n\r\t\f\v");
    return (end == string::npos) ? "" : s.substr(0, end + 1);
}
 
string trim(const string &s) {
    return rtrim(ltrim(s));
}

vector<string> tokenize(string inp) {
    vector<string> res;
    regex reg("([^\"]\\S*|\".+?\")\\s*");
    smatch match;
    while(regex_search(inp, match, reg)) {
        string str = trim(match.str());
        if(str[0] == '"')
            str = str.substr(1, str.size()-2);

        res.push_back(str);
        inp = match.suffix().str();   
    }
    for(int i=1; i<res.size(); ++i)
        res[i] = expand_tilde(res[i]);

    return res;
}

/* Copy command */
void command_copy(vector<string> &args) {
    if(args.size() < 3) {
        set_status("invalid number of args", true);
        return;
    }

    string dest_dir = args[args.size()-1];
    if(IS_RELATIVE(dest_dir))
        dest_dir = join(cdir, dest_dir);
    // string sts;
    for(int i=1; i<args.size()-1; ++i) {
        string src = args[i];
        if(IS_RELATIVE(src))
            src = join(cdir, src);

        int status;
        if(is_dir(src))
            status = copy_dir(src, dest_dir);
        else
            status = copy_file(src, dest_dir);

        if(status == 0)
            set_status("copied", true);
        else
            set_status("failed to copy: "+to_string(status), true);
    }
}

/* move command */
void command_move(vector<string> &args) {
    if(args.size() < 3) {
        set_status("invalid number of args", true);
        return;
    }
    /* move when more than 3 args are present */
    string dest_dir = args[args.size()-1];
    if(IS_RELATIVE(dest_dir))
        dest_dir = join(cdir, dest_dir);
    // string ts;
    for(int i=1; i<args.size()-1; ++i) {
        string src = args[i];
        if(IS_RELATIVE(src))
            src = join(cdir, src);
        string dest = join(dest_dir, FILE_NAME(src));

        /* move the file(s) */
        if(rename(src.c_str(), dest.c_str()) == 0)
            set_status("moved", true);
        else
            set_status("failed to move: "+to_string(errno), true);
    }
}

/* rename command */
void command_rename(vector<string> &args) {
    if(args.size() != 3) {
        set_status("invalid number of args", true);
        return;
    }
    string src = args[1];
    if(IS_RELATIVE(src))
        src = join(cdir, src);
    string dest = args[2];
    if(IS_RELATIVE(dest))
        dest = join(cdir, dest);
    
    /* rename the file */
    if(rename(src.c_str(), dest.c_str()) == 0)
        set_status("renamed", true);
    else
        set_status("failed to rename: error " + to_string(errno), true);
}

/* create command */
void command_create_file(vector<string> &args) {
    if(args.size() != 3) {
        set_status("invalid number of args", true);
        return;
    }
    string dest = args[2];
    if(IS_RELATIVE(dest))
        dest = join(cdir, dest);
    if(create_file(args[1], dest) == 0)
        set_status("created", true);
    else
        set_status("failed to create: error " + to_string(errno), true);
}

/* delete command */
void command_delete(vector<string> &args) {
    if(args.size() < 2) {
        set_status("invalid number of args", true);
        return;
    }
    string path = args[1];
    if(IS_RELATIVE(path))
        path = join(cdir, path);
    
    int ret_val;
    if(is_dir(path))
        ret_val = delete_dir(path);
    else 
        ret_val = delete_file(path);
    if(ret_val == 0)
        set_status("deleted", true);
    else
        set_status("failed to delete: error " + to_string(errno), true);
}

/* goto command */
void command_goto(vector<string> &args) {
    if(args.size() < 2) {
        set_status("invalid number of args", true);
        return;
    }
    string path = args[1];
    if(IS_RELATIVE(path))
        path = join(cdir, path);
    
    if(!goto_dir(path))
        set_status("path not found", true);
}

/* mkdir command */
void command_create_dir(vector<string> &args) {
    if(args.size()<3) {
        set_status("invalid number of args", true);
        return;
    }
    string dirname = args[1];
    string dest_path = args[2];
    if(IS_RELATIVE(dest_path))
        dest_path = join(cdir, dest_path);
    if(create_dir(dirname, dest_path) == 0)
        set_status("created successfully", true);
    else
        set_status("falied to create dir", true);
}

/* search command */
void command_search(vector<string> args) {
    if(args.size()<2) {
        set_status("invalid number of args", true);
        return;
    }
    if(search_file(cdir, args[1]) != "")
        set_status("True", true);
    else   
        set_status("False", true);
}

void restore_old_config() {
    CLEAR();
    tcsetattr(fd, TCSAFLUSH, &tty_config_old);
    close(fd);
}

/* assign intial values to global variables */
void init() {
    home = join("/home", getenv("USERNAME"));

    cdir = app_home;

    top_offset = 2;
    bottom_offset = 2;
}

/*
    Main function
*/
int main() {
    /* open /dev/tty */
    fd = open(tty, O_RDWR | O_NDELAY | O_NOCTTY);
    if(!isatty(fd)) {
        fprintf(stderr, "failed to open tty\n");
        return 0;
    }
    
    /* get the current tty configuration */
    if(tcgetattr(fd, &tty_config)<0) {
        fprintf(stderr, "unable to get tty config\n");
        return 0;
    }
    tty_config_old = tty_config;
    atexit(restore_old_config);

    /* get window size config */
    if(ioctl(fd, TIOCGWINSZ, &tty_ws) < 0) {
        int error = errno;
        fprintf(stderr, "unable to get window size\n");
        fprintf(stderr, "%d\n", error);
        return 0;
    }

    /* set signal for window resize */
    signal(SIGWINCH, signal_win_resize);

    /* get app_home and current working directory */
    app_home = getcwd(NULL, 0);
    // cdir = app_home;

    init();

    /* Enter normal mode */
    activate_normal_mode();
    set_status("Normal Mode", true);

    /* set top as cdir */
    char ch;
    while(1) {
        cin.get(ch);
        if(mode == NORMAL_MODE) {
            /* Normal Mode processing */
            switch(ch) {
                case 'l': {
                    scroll_down();
                    break;
                }
                case 'k': {
                    scroll_up();
                    break;
                }
                case '\n': {
                    enter();
                    break;
                }
                case 127: {
                    go_back();
                    break;
                }
                case 'D': {
                    go_left();
                    break;
                }
                case 'C': {
                    go_right();
                    break;
                }
                case 'h' : {
                    goto_dir(home);
                    break;
                }
                case ':': {
                    activate_command_mode();
                    break;
                }
                default: break;
            }
            if(ch == 'q') break;
        } else {
            /* Command Mode Processing */
            if(ch == 27) {
                /* activate normal mode when ESC is pressed */
                inp.clear();
                activate_normal_mode();
                set_status("Normal Mode", true);
            } else if(ch == 127) {
                /* backspace */
                if(inp.size()>0) {
                    cout<<"\b \b";
                    inp.pop_back();
                }
            } else {
                /* keep storing input until \n is met */
                if(ch != '\n') {
                    cout.put(ch);
                    inp += ch;
                    continue;
                }
                
                /* split the input string in to command args */
                vector<string> args = tokenize(inp);

                if(args[0] == "copy") {
                    command_copy(args);
                } else if(args[0] == "move") {
                    command_move(args);
                } else if(args[0] == "rename") {
                    command_rename(args);
                } else if(args[0] == "create_file") {
                    command_create_file(args);
                } else if(args[0] == "delete") {
                    command_delete(args);
                } else if(args[0] == "create_dir") {
                    command_create_dir(args);
                } else if(args[0] == "search") {
                    command_search(args);
                } else if(args[0] == "goto") {
                    command_goto(args);
                } else {
                    set_status("invalid command", true);
                }
                inp.clear();
                activate_normal_mode();
            }
        }
    }
}