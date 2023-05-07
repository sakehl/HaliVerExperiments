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
    return l == "" or l.startswith("{") or l.startswith("}")

def count_lines(name: str, version: str, is_front: bool = False) -> LineInfo:
    lines_of_code: int = 0
    nr_annotations: int = 0
    loops: int = 0
    if is_front:
        pvl_name = 'build/' + name + '_front_pvl.pvl'
    else:
        pvl_name = 'build/' + name + '_pvl-' + version + '.pvl'
    with open(pvl_name) as f:
        for line in f:
            # skip empty lines or lines with only braces
            if is_none_line(line):
               continue
            # Skip helper functions 
            if not is_front and is_other_func(line):
                lines_of_code = 0
                nr_annotations = 0
                loops = 0
                continue
            
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
    failtime = 0.0
    timeouts = 0
    for (t,r) in xs:
        if(r == "pass"):
            total += t
            passes += 1
        elif(r == "fail"):
            failtime += t
            fails += 1
        elif(r == "timeout"):
            timeouts += 1
    avr_total = total / passes if passes != 0 else None
    avr_fails = failtime / fails if fails != 0 else None
    return avr_total, passes, failtime, fails, timeouts


class Encoder(json.JSONEncoder):
    def default(self, o):
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

def read_results(prefix: str, name: str) -> Tuple[VerificationResults, Dict[str,float], Dict[str, LineInfo]]:
    pre = f'{prefix}_{name}'
    with open(pre + "_results_verification.json") as f:
        results_verification = json.loads(f.read(), object_hook=as_ver_result)
        
    with open(pre + '_compilation.json') as f:
        compilation = json.loads(f.read(), object_hook=as_line_info)
        
    with open(pre + '_line_info.json') as f:
        line_info = json.loads(f.read(), object_hook=as_line_info)
        
    return results_verification, compilation, line_info

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
    def __init__(self, versions: Dict[str, List[str]], repetitions: int = 5, timeout: int = 10*60):
        self.versions: Dict[str, List[str]] = versions
        self.compilation_times: Dict[str, Dict[str,float]] = {}
        self.line_infos: Dict[str, Dict[str,LineInfo]] = {}
        self.verification_times: Dict[str, Dict[str, List[Tuple[float, Result]]]] = {}

        self.repetitions = repetitions
        self.timeout = timeout

        for name in self.versions:
            self.verification_times[name] = {}
            self.compilation_times[name] = {}
            self.line_infos[name] = {}

    def __str__(self) -> None:
        result = "{"
        result += "verification_times: " + str(self.verification_times)
        result += "\n, compilation_times: " + str(self.compilation_times)
        result += "\n, line_infos: " + str(self.compilation_times)
        result += "\n}"
        return result



    def make_files(self, name: str) -> None:
        try:
            pvl_name = 'build/' + name + '_pvl.pvl'
            compileTimeStage1, _ = runCommand('cmake --build build -t ' + name + ' --clean-first')
        
            for v in self.versions[name]:
                compileTimeStage2 = runCommand('cd build && ./' + name + ' ' + v)[0]
                self.compilation_times[name][v] = compileTimeStage1 + compileTimeStage2
                print("Compilation time ", v, compileTimeStage1 + compileTimeStage2)
                line_info = count_lines(name, v)
                self.line_infos[name][v] = line_info
                print(line_info)
                
            compileTimeStage2 = runCommand('cd build && ./' + name + ' ' + 'front')[0]
            self.compilation_times[name]['front'] = compileTimeStage1 + compileTimeStage2
            print("Compilation time front", compileTimeStage1 + compileTimeStage2)
        except Exception as e:
            print(e)

    def front_end(self, name: str, repetitions: Optional[int] = None, timeout: Optional[int] = None) -> None:
        n = self.repetitions if repetitions == None else repetitions
        t = self.timeout if timeout == None else timeout
        try:
            # Front-end verification
            pvl_name = name + '_front_pvl.pvl'
            self.verification_times[name]['front'] = []
            for i in range(0,n):
                verificationTime = runCommand('vct --backend-option --conditionalizePermissions --silicon-quiet build/' + pvl_name, timeout=t)
                self.verification_times[name]['front'].append(verificationTime)
                print(verificationTime)
        except Exception as e:
            print(e)

    def back_end(self, name: str, repetitions: Optional[int] = None, timeout: Optional[int] = None, version: str ='all') -> None:
        n = self.repetitions if repetitions == None else repetitions
        t = self.timeout if timeout == None else timeout
        try:
            testing = self.versions[name] if version == 'all' else [version]
            for v in testing:
                pvl_name = name + '_pvl-' + v + '.pvl'
                self.verification_times[name][v] = []
                for i in range(0,n):
                    verificationTime = runCommand('vct --backend-option --conditionalizePermissions --silicon-quiet build/' + pvl_name, timeout=t)
                    self.verification_times[name][v].append(verificationTime)
                    print(verificationTime)
        except Exception as e:
            print(e)

    def save_results(self, prefix: str, name: str) -> None:
        pre = f'{prefix}_{name}'
        with open(pre + '_results_verification.json', 'w') as f:
            f.write(json.dumps(self.verification_times[name]))
        with open(pre + '_compilation.json', 'w') as f:
            f.write(json.dumps(self.compilation_times[name]))
        with open(pre + '_line_info.json', 'w') as f:
            f.write(json.dumps(self.line_infos[name], cls=Encoder))

    def save_all_results(self, prefix: str) -> None:
        for n in self.versions:
            self.save_result(prefix, n)

    def load_results(self, prefix, name) -> None:
        results_verification, compilation, line_info = read_results(prefix, name)
        self.verification_times[name] = results_verification
        self.compilation_times[name] = compilation
        self.line_infos[name] = line_info

    def load_all_results(self, prefix: str) -> None:
        for n in self.versions:
            self.load_results(prefix, n)

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
                if v == self.versions[name][0]:
                    shortname = name
                    if name == 'conv_layer':
                        shortname = 'conv\_'
                    if name == 'auto_viz':
                        shortname = 'auto\_'
                    t, passes, failt, fils, timeouts = get_average(self.verification_times[name]['front'])
                    row += f"{shortname} & V{v} & {halideLoC[name]} & {halideAnn[name]} & {round(t)} & 0 & "
                else:
                    if name == 'conv_layer' and v == self.versions[name][1]:
                        row += "layer"
                    if name == 'auto_viz' and v == self.versions[name][1]:
                        row += "viz"
                    row += f" & V{v}-{directivesUsed[name][v]} & \ditto & \ditto & \ditto &"
                    row += f"{scheduleLoC[name][v]} &"
                    
                full_name = name + '-' + v
                
                row += f"{self.line_infos[name][v].lines_of_code} &"
                row += f"{self.line_infos[name][v].nr_annotations} &" 
                row += f"{self.line_infos[name][v].loops} & "
                
                res = self.verification_times[name][v]
                t, passes, failtime, fails, timeouts = get_average(res)
                if((passes > 0 and passes < len(res))):
                    print(f"inconsistent results for {name} version V{v}")
                    print(f"avr_total, passes, failtime, fails, timeouts: {t, passes, failtime, fails, timeouts}")
                    row += " F & "
                if(passes == len(res) and t != None):
                    row += f"{round(t)} & "
                elif(fails > 0):
                    row += " F & "
                elif(timeouts > 0):
                    row += "T.O. & "
                row += str(round(self.line_infos[name][v].nr_annotations / halideAnn[name], 1)) + "x" + r"\\ \hline"
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
  def __init__(self, pdf, size=(200,200)):
    self.pdf = pdf
    self.size = size

  def _repr_html_(self):
    return '<iframe src={0} width={1[0]} height={1[1]}></iframe>'.format(self.pdf, self.size)