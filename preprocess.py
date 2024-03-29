from typing import List, Optional, Dict, Tuple, Any, Union
from collections import Counter, defaultdict
from enum import Enum
import subprocess
import time
import datetime
import json

class LineInfo:
    def __init__(self, lines_of_code: int, nr_annotations: int, loops: int):
        self.lines_of_code: int = lines_of_code
        self.nr_annotations: int = nr_annotations
        self.loops: int = loops
    
    def __str__(self) -> str:
        return f"Code: {self.lines_of_code}\nAnnotations:{self.nr_annotations}\nLoops: {self.loops}"

    def __repr__(self) -> str:
        return f"LineInfo({self.lines_of_code}, {self.nr_annotations}, {self.loops})"

class Result(str,Enum):
    Pass = 'pass'
    Fail = 'fail'
    Error = 'error'
    TimeOut = 'timeout'

def is_annotation(line: str) -> bool:
    l = line.lstrip()
    return l.startswith("requires ") or l.startswith("ensures ") or \
        l.startswith("context ") or l.startswith("context_everywhere ") or \
        l.startswith("loop_invariant ")

def is_loop(line: str)-> bool:
    l = line.lstrip()
    return l.startswith("par ") or l.startswith("for ") or l.startswith("for(")

# Other functions that are defined alway start with pure
def is_other_func(line: str) -> bool:
    l = line.lstrip()
    return l.startswith("pure ")

# Do not count empty lines, or lines that start with "{" or "}"
def is_none_line(line: str) -> bool:
    l = line.strip()
    return l == "" or l.startswith("{") or l.startswith("}") or l.startswith("//")

# Count the number of lines of code, annotations and loops
# A line is either code or annotation, not both. So  lines of code + lines of annotations should be the complete number of lines.
def count_lines(name: str) -> LineInfo:
    lines_of_code: int = 0
    nr_annotations: int = 0
    loops: int = 0
    with open(name) as f:
        for line in f:
            if is_annotation(line):
                nr_annotations += 1
            else:
                lines_of_code += 1
            
            if is_loop(line):
                loops += 1
    return LineInfo(lines_of_code, nr_annotations, loops)

def get_average(xs: List[Tuple[float, Result]]) -> Tuple[Optional[float], Optional[float], Optional[float], int, int, int]:
    total = 0.0
    passes = 0
    fails = 0
    passtime = 0.0
    failtime = 0.0
    timeouts = 0
    for (t,r) in xs:
        if(r == "pass"):
            total += t
            passtime += t
            passes += 1
        elif(r == "fail"):
            total += t
            failtime += t
            fails += 1
        elif(r == "timeout"):
            timeouts += 1
    avr_total = total / (passes + fails) if (passes + fails) != 0 else None
    avr_pass = passtime / passes if passes != 0 else None
    avr_fails = failtime / fails if fails != 0 else None
    return avr_total, avr_pass, avr_fails, passes, fails,  timeouts

def average_to_str(xs: List[Tuple[float, Result]], name: str)-> str:
    t, passtime, failtime, passes, fails,  timeouts = get_average(xs)
    res: str
    if((passes > 0 and passes < len(xs))):
        print(f"inconsistent results for '{name}'")
        print(f"avr_total, passes, fails,  timeouts, passtime, failtime: {t, passes, fails,  timeouts, passtime, failtime}")
        res = str(round(t)) + "$^{\dag}$" # type: ignore
    elif(passes == len(xs) and t != None):
        res = f"{round(t)}" # type: ignore
    elif(fails > 0):
        res = "F"
    elif(timeouts > 0):
        res = "T.O."
    return res


class Encoder(json.JSONEncoder):
    def default(self, o): # type: ignore
        if(isinstance(o, LineInfo)):
            return o.__dict__
        return json.JSONEncoder.default(self, o)

VerificationResults = Dict[str,List[Tuple[float,Result]]]

def as_ver_result(dct: Dict[str, Any]) -> VerificationResults:
    result: Dict[str, List[Tuple[float, Result]]] = {}
    for k in dct:
        res_list: List[Tuple[float, Result]] = []
        for time, res in dct[k]:
            res_list.append((float(time), Result(res)))
        result[k] = res_list
    return result

def as_line_info(dct: Dict[str, Any]) -> Union[Dict[str,Any], LineInfo]:
    if('lines_of_code' in dct):
        return LineInfo(**dct)
    else:
        return dct

def read_results(prefix: str) -> Tuple[VerificationResults, Dict[str, LineInfo]]:
    pre = f'{prefix}'
    with open(pre + "_results_verification.json") as f:
        results_verification = json.loads(f.read(), object_hook=as_ver_result)
        
    with open(pre + '_line_info.json') as f:
        line_info = json.loads(f.read(), object_hook=as_line_info)
        
    return results_verification, line_info

def runCommand(command: List[str], log_file: Optional[str] = None, verbose: bool = False, timeout: Optional[int] = None)-> Tuple[float, Result]:
    start = time.time()
    out: Optional[str] = None
    err_out: Optional[str] = None
    try:
        p = subprocess.run(command, capture_output=True, timeout=timeout, text=True)
    except subprocess.TimeoutExpired as to:
        end = time.time()
        if(to.stdout):
            out = to.stdout.decode("utf-8")
        if(to.stderr):
            err_out = to.stderr.decode("utf-8")
        res = Result.TimeOut
    else:
        end = time.time()
        out = p.stdout
        err_out = p.stderr
        if p.returncode == 0:
            res = Result.Pass
        elif p.returncode == 1:
            res = Result.Fail
        elif p.returncode == 2:
            res = Result.Error
        elif p.returncode == 3:
            res = Result.TimeOut
        else:
            res = Result.Fail
            print("Got unexpected return code '" + str(p.returncode) + "' for command '" + " ".join(command) + "'")

    if(res == Result.Fail):
        print(f"Verification Error, see logfile '{log_file}' for details")
    
    if(verbose):
        if(out):
            print(out)
        if(err_out):
            print(err_out)
    if(log_file):
        with open(log_file, "a") as f:
            f.write("="*80 + "\n")
            f.write(" ".join(command) + "\n")
            f.write(datetime.datetime.fromtimestamp(start).strftime('%Y-%m-%d %H:%M:%S')+ "\n")
            f.write(f"Result: {res}, time: {end - start} seconds \n")
            if(out):
                f.write("="*36 +" Stdout " + "="*36 + "\n")
                f.write
                f.write(out)
            if(err_out):
                f.write("="*36 +" Stderr " + "="*36 + "\n")
                f.write(err_out)

    return end - start, res

class Experiments:
    def __init__(self, versions: Dict[str, List[str]], mem_versions: Dict[str, List[str]], vercors_loc: str, silicon_loc: str, repetitions: int = 5, timeout: int = 10*60):
        self.versions: Dict[str, List[str]] = versions
        self.mem_versions: Dict[str, List[str]] = mem_versions
        self.line_infos: Dict[str,LineInfo] = {}
        self.verification_times: Dict[str, List[Tuple[float, Result]]] = {}

        self.vercors_loc = vercors_loc
        self.silicon_loc = silicon_loc

        self.repetitions = repetitions
        self.timeout = timeout

    def __str__(self) -> str:
        result = "{"
        result += "verification_times: " + str(self.verification_times)
        result += "\n}"
        return result

    def version_to_file_name(self, name: str, version: str, mem: bool = False) -> str:
        ext = '.pvl' if version == 'front' else '.c'
        return name + ('_' + version if version != '' else '') + ('_mem' if mem else '') + ext

    def count_files(self) -> None:
        for n in self.versions:
            for v in self.versions[n]:
                name = self.version_to_file_name(n, v)
                line_info = count_lines("build/" + name)
                self.line_infos[name] = line_info
                print(name)
                print(line_info)
            # Count front file
            v = 'front'
            name = self.version_to_file_name(n, v)
            line_info = count_lines("build/" + name)
            self.line_infos[name] = line_info
            print(name)
            print(line_info)
        for n in self.mem_versions:
            for v in self.mem_versions[n]:
                name = self.version_to_file_name(n, v, True)
                line_info = count_lines("build/" + name)
                self.line_infos[name] = line_info
                print(name)
                print(line_info)
    
    def run_verification(self, file_name: str, repetitions: Optional[int] = None, timeout: Optional[int] = None, useAPI= False)-> None:
        n: int = repetitions if repetitions != None else  self.repetitions # type: ignore
        t = timeout if timeout != None else self.timeout
        command = [self.vercors_loc ,"--dev-assert-timeout", "0"] + \
                (["--backend-option", "--prover=Z3-API"] if useAPI else []) + \
                ["--silicon-quiet"
                ,"--no-infer-heap-context-into-frame"
                ,"--dev-total-timeout", str(t)
                ,"--backend-file-base" ,"build/" + file_name 
                ,"build/" + file_name]
        try:
            self.verification_times[file_name] = []
            for i in range(0,n):
                verificationTime = runCommand(command, log_file="logs/"+ file_name + ".txt")
                self.verification_times[file_name].append(verificationTime)
                print(file_name, verificationTime)
        except Exception as e:
            print(e)

    def run_silicon(self, file_name: str, repetitions: Optional[int] = None, timeout: Optional[int] = None)-> None:
        n: int = repetitions if repetitions != None else  self.repetitions # type: ignore
        t = timeout if timeout != None else self.timeout
        silicon_command = [self.silicon_loc, "--logLevel", "INFO", "--timeout", str(t)]
        vpr_fn = file_name + "-0.vpr"
        try:
            self.verification_times[vpr_fn] = []
            for i in range(0,n):
                verificationTime = runCommand(silicon_command + ["build/" + vpr_fn]
                                                , log_file="logs/"+ vpr_fn + ".txt"
                                              )
                self.verification_times[vpr_fn].append(verificationTime)
                print(vpr_fn, verificationTime)
        except Exception as e:
            print(e)

    def front_end(self, name: str, repetitions: Optional[int] = None, timeout: Optional[int] = None) -> None:
        pvl_name = self.version_to_file_name(name, 'front')
        self.run_verification(pvl_name, repetitions, timeout)

    def back_end(self, name: str, mem: bool = False, repetitions: Optional[int] = None, timeout: Optional[int] = None, version: str ='all') -> None:
        if mem:
            testing = self.mem_versions[name] if version == 'all' else [version]
        else:
            testing = self.versions[name] if version == 'all' else [version]
        for v in testing:
            filename = self.version_to_file_name(name, v, mem)
            self.run_verification(filename, repetitions, timeout, useAPI=True)

    def save_results(self, prefix: str) -> None:
        pre = f'{prefix}'
        with open(pre + '_results_verification.json', 'w') as f:
            f.write(json.dumps(self.verification_times))
        with open(pre + '_line_info.json', 'w') as f:
            f.write(json.dumps(self.line_infos, cls=Encoder))

    def load_results(self, prefix: str) -> None:
        results_verification, line_info = read_results(prefix)
        self.verification_times = results_verification
        self.line_infos = line_info

    def make_table(self, directivesUsed: Dict[str, Dict[str,str]], halideLoC: Dict[str, int], 
            halideAnn: Dict[str, int], scheduleLoC: Dict[str, Dict[str,int]]
            ) -> str:
        header0 = r"\begin{tabular}{l l \vbar \vbar r r \vbar r \vbar r \vbar r r r r \vbar \vbar r}"
        header1 = r"\hline Name & & \multicolumn{2}{l\vbar}{\halide} & \multicolumn{1}{l\vbar}{Fr-end} & Sched. & \multicolumn{3}{l}{\C} & & LoA \\"
        header2 = r"& & LoC & LoA & T. (s) & LoC & LoC & LoA & Loops & T. (s) & incr. \\ \hline \hline"

        rows: List[str] = [header0, header1, header2]
        for name in self.versions:
            for v in self.versions[name]:
                row = ""
                filename = self.version_to_file_name(name, v)

                if v == self.versions[name][0]:
                    shortname = name
                    if name == 'conv_layer':
                        shortname = 'conv\_'
                    if name == 'auto_viz':
                        shortname = 'auto\_'
                    front_filename = self.version_to_file_name(name, 'front')
                    t = average_to_str(self.verification_times[front_filename], front_filename)
                    row += f"{shortname} & V{v} & {halideLoC[name]} & {halideAnn[name]} & {t} & 0 & "
                else:
                    if name == 'conv_layer' and v == self.versions[name][1]:
                        row += "layer"
                    if name == 'auto_viz' and v == self.versions[name][1]:
                        row += "viz"
                    row += f" & V{v}-{directivesUsed[name][v]} & \ditto & \ditto & \ditto &"
                    row += f"{scheduleLoC[name][v]} &"
                    
                full_name = name + '-' + v
                
                row += f"{self.line_infos[filename].lines_of_code} &"
                row += f"{self.line_infos[filename].nr_annotations} &" 
                row += f"{self.line_infos[filename].loops} & "
                
                res = self.verification_times[filename]
                t = average_to_str(self.verification_times[filename], filename)
                row += f"{t} & "
                row += str(round(self.line_infos[filename].nr_annotations / halideAnn[name], 1)) + "x" + r"\\ \hline"
                rows.append(row)
            rows.append ("\hline")
        rows.append(r"\end{tabular}")
        return "\n".join(rows)

    def save_table(self, directivesUsed: Dict[str, Dict[str,str]], halideLoC: Dict[str, int], 
            halideAnn: Dict[str, int], scheduleLoC: Dict[str, Dict[str,int]]
            ) -> None:
        table = self.make_table(directivesUsed, halideLoC, halideAnn, scheduleLoC)

        with open('result_table.tex', 'w') as f:
            f.write(table)

# From https://stackoverflow.com/questions/19470099/view-pdf-image-in-an-ipython-notebook
class PDF(object):
  def __init__(self, pdf, size=(200,200)): # type: ignore
    self.pdf = pdf
    self.size = size

  def _repr_html_(self)->str:
    return '<iframe src={0} width={1[0]} height={1[1]}></iframe>'.format(self.pdf, self.size)
  

def count_schedule_directives(line: str)-> Counter[str]:
    directives = {"parallel", "vectorize", "unroll", 
                  "split", "tile" , "fuse", "reorder",
                  "compute_root", "compute_at",
                  "store_at", "store_root",
                  "fold_storage","compute_with","prefetch",
                  "bound_extent", "bound", "rename", "update"}
    result = Counter()
    for d in directives:
        i = line.count("."+d+"(")
        if(i > 0):
            result[d] += 1
    return result

def count_annotations(line: str)->int:
    directives = {"ensures", "context", "requires", "invariant"}
    result = 0
    for d in directives:
        result += line.count(d+"(")
    return result

def is_schedule(line: str)-> Optional[int]:
    l = line.strip()
    schedule = 0
    if(l == "/* End Schedule */"):
        return -1
    elif(l == "/* Schedule */"):
        return 0
    elif(l.startswith("/* Schedule") and l.endswith("*/")):
        l = l[len("/* Schedule"):-2]
        try:
            return int(l)
        except:
            print(f"Error reading line {line}")
            return 0
    else:
        return None
    
def count_cpp_file(f: str)-> Tuple[int, int, Dict[int, Counter[str]]]:
    anns = 0
    sched = 0
    sched_dict = {-1: Counter(), 0: Counter(), 1: Counter(), 2: Counter(), 3: Counter(), 4: Counter()}
    loc = 0
    with open(f) as f:
        current_sched = -1
        for l in f:
            l = l.strip().split("//")[0]

            next_sched = is_schedule(l)
            if(next_sched != None):
                current_sched = next_sched
            else:
                i = count_annotations(l)
                anns += i
                sched_dict[current_sched] += count_schedule_directives(l)
                # Do not count lines which are part of the schedule or the annotations
                if(current_sched == -1 and i == 0):
                    loc += 1
    return anns, loc, sched_dict

def get_directives(sched: Counter[str])-> str:
    result = []
    if 'compute_at' in sched or 'compute_root' in sched:
        result.append('c')
    if 'fuse' in sched:
        result.append('f')
    if 'parallel' in sched:
        result.append('p')
    if 'reorder' in sched:
        result.append('r')
    if 'split' in sched or 'tile' in sched:
        result.append('s')
    if 'store_at' in sched or 'store_root' in sched:
        result.append('st')
    if 'unroll' in sched:
        result.append('u')
    
    return '\{' + ','.join(result) + '\}'

def make_normal_table(experiments: Experiments)->str:
    directivesUsed : Dict[str, Dict[str,str]] = {}
    scheduleLoC: Dict[str, Dict[str,int]] =  {}
    halideAnn: Dict[str, int] = {}
    halideLoC: Dict[str, int]  = {}

    for name in experiments.versions:
        fn = "src/" + name + ".cpp" 
        anns, loc, sched_dict = count_cpp_file(fn)
        halideAnn[name] = anns
        halideLoC[name] = loc
        # 'bound' can also occur outside the schedule, which is fine
        if sum(sched_dict[-1].values()) != 0 and sched_dict[-1].keys() != {'bound'}:
            print(f"Found non-zero count for outside schedule for {name}: {sched_dict[-1]}")
        directivesUsed[name] = defaultdict(str)
        scheduleLoC[name] = defaultdict(int)
        for v in experiments.versions[name]:
            directivesUsed[name][v] = get_directives(sched_dict[int(v)])
            scheduleLoC[name][v] = sum(sched_dict[int(v)].values())

    return experiments.make_table(directivesUsed, halideLoC, halideAnn, scheduleLoC)

def make_mem_table(experiments: Experiments)->str:
    directivesUsedMem : Dict[str, Dict[str,str]] = {}
    scheduleLoCMem: Dict[str, Dict[str,int]] = {}
    halideLoCMem: Dict[str, int]  = {}

    for name in experiments.mem_versions:
        fn = "src/" + name + ".cpp" 
        anns, loc, sched_dict = count_cpp_file(fn)
        halideLoCMem[name] = loc
        
        # 'bound' can also occur outside the schedule, which is fine
        if sum(sched_dict[-1].values()) != 0 and sched_dict[-1].keys() != {'bound'}:
            print(f"Found non-zero count for outside schedule for {name}: {sched_dict[-1]}")
        directivesUsedMem[name] = { }
        scheduleLoCMem[name] = {}
        for v in experiments.mem_versions[name]:
            version = int(v) if v != "" else 0
            directivesUsedMem[name][v] = get_directives(sched_dict[version])
            scheduleLoCMem[name][v] = sum(sched_dict[version].values())

    header0 = r"\begin{tabular}{l l \vbar \vbar r \vbar r \vbar r r r r}"
    header1 = r"\hline \textbf{Name} & & \multicolumn{1}{l\vbar}{\textbf{\halide}} & \textbf{\textbf{Sched}}. & \multicolumn{3}{l}{\textbf{\c}} & \\"
    header2 = r"& & \textbf{LoC} & \textbf{Dir.} & \textbf{LoC} & \textbf{Ann.} & \textbf{Loops} & \textbf{T. (s).} \\ \hline \hline"

    rows: List[str] = [header0, header1, header2]
    for name in experiments.mem_versions:
        for v in experiments.mem_versions[name]:
            filename = experiments.version_to_file_name(name, v, True)
            solo_bench = len(experiments.mem_versions[name]) == 1
            vname = "" if solo_bench == 1 else "V" + v
            row = ""
            if v == experiments.mem_versions[name][0]:
                shortname = name
                if name == 'conv_layer':
                    shortname = 'conv\_'
                if name == 'auto_viz':
                    shortname = 'auto\_'
                LoC = "?" if solo_bench else "0"
                if solo_bench:
                    row += "\multicolumn{2}{l \\vbar \\vbar}{" + shortname.replace('_', '\_') + f"-{directivesUsedMem[name][v]} }} &"
                else:
                    row += f"{shortname} & {vname} &"
                row += f" {halideLoCMem[name]} & {scheduleLoCMem[name][v]} & "
            else:
                if name == 'conv_layer' and v == experiments.mem_versions[name][1]:
                    row += "layer"
                if name == 'auto_viz' and v == experiments.mem_versions[name][1]:
                    row += "viz"
                row += f" & {vname}-{directivesUsedMem[name][v]} & \ditto &"
                row += f"{scheduleLoCMem[name][v]} &"

            full_name = name + '-' + v

            row += f"{experiments.line_infos[filename].lines_of_code} &"
            row += f"{experiments.line_infos[filename].nr_annotations} &" 
            row += f"{experiments.line_infos[filename].loops} & "

            res = experiments.verification_times[filename]
            row += average_to_str(res, filename)
            row += r"\\ \hline"
            rows.append(row)
        rows.append ("\hline")
    rows.append(r"\end{tabular}")
    mem_table = "\n".join(rows)
    return mem_table