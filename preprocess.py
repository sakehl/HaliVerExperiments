from typing import List, Optional, Dict, Tuple, Any, Union
from enum import Enum
import subprocess
import os
import time
import json
import atexit

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

def get_average(xs: List[Tuple[float, Result]]) -> Tuple[Optional[float], int, Optional[float], int, int]:
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
    passtime = passtime / passes if passes != 0 else None
    avr_fails = failtime / fails if fails != 0 else None
    return avr_total, passes, fails,  timeouts, passtime, failtime

def average_to_str(xs: List[Tuple[float, Result]], name: str)-> str:
    t, passes, fails,  timeouts, passtime, failtime = get_average(xs)
    res: str
    if((passes > 0 and passes < len(xs))):
        print(f"inconsistent results for '{name}'")
        print(f"avr_total, passes, fails,  timeouts, passtime, failtime: {t, passes, fails,  timeouts, passtime, failtime}")
        res = str(round(t)) + "$^{\dag}$"
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

def runCommand(command: str, verbose: bool = False, timeout: Optional[int] = None)-> Tuple[float, Result]:
    start = time.time()
    try:
        p = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, timeout=timeout)
    except subprocess.TimeoutExpired as to:
        print(f"Process was stopped after {to.timeout} seconds")
        if(to.stdout):
            print(to.stdout.decode('UTF-8'))
        end = time.time()
        return end - start, Result.TimeOut
        
    end = time.time()
    if(p.returncode != 0):
        print(p.stdout.decode('UTF-8'))
        print(f"Return code was {p.returncode}")
        return end - start, Result.Fail
    elif verbose:
        print(p.stdout.decode('UTF-8'))
        
    return end - start, Result.Pass

class Experiments:
    def __init__(self, versions: Dict[str, List[str]], mem_versions: Dict[str, List[str]], repetitions: int = 5, timeout: int = 10*60):
        self.versions: Dict[str, List[str]] = versions
        self.mem_versions: Dict[str, List[str]] = mem_versions
        self.line_infos: Dict[str,LineInfo] = {}
        self.verification_times: Dict[str, List[Tuple[float, Result]]] = {}

        self.repetitions = repetitions
        self.timeout = timeout

    def __str__(self) -> str:
        result = "{"
        result += "verification_times: " + str(self.verification_times)
        result += "\n}"
        return result

    def version_to_file_name(self, name: str, version: str, mem: bool = False) -> str:
        return name + ('_' + version if version != '' else '') + ('_mem' if mem else '') + '.pvl'

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
    
    def run_verification(self, file_name: str, repetitions: Optional[int] = None, timeout: Optional[int] = None)-> None:
        n: int = repetitions if repetitions != None else  self.repetitions # type: ignore
        t = timeout if timeout != None else self.timeout
        try:
            self.verification_times[file_name] = []
            for i in range(0,n):
                verificationTime = runCommand('vct --backend-option --conditionalizePermissions --silicon-quiet build/' + file_name, timeout=t)
                self.verification_times[file_name].append(verificationTime)
                print(verificationTime)
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
            pvl_name = self.version_to_file_name(name, v, mem)
            self.run_verification(pvl_name, repetitions, timeout)

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
        header1 = r"\hline Name & & \multicolumn{2}{l\vbar}{\halide} & \multicolumn{1}{l\vbar}{Fr-end} & Sched. & \multicolumn{3}{l}{\pvl} & & LoA \\"
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
                    t = average_to_str(self.verification_times[filename], filename)
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