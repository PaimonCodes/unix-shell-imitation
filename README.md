# unix-shell-imitation
A C program that runs an imitation of the unix shell.

---
## Functionality
### Shell launch
The shell is launched with `gcc -o wish wish.c -Wall -Werror`. The shell can support two launch modes. The shell supports an interactive mode
where the user typically writes the commands themselves, or they can opt to use a text file to read commands from. 

Use `./wish` to launch interactive mode, or use `./wish <file.txt>` to read commands from a file. 
The shell will exit automatically regardless if exit is called or not in the text file. The text file should have an extra newline or return carriage at the end of 
file for the read to work properly.

### Built-in commands
`cd` allows for the shell user to change the current workspace directory, much like with the `cd` unix command that we all know and love.

`path` is where programs are searched for, accesed, and ran. By default this is set to `/bin`. Shell users can update this PATH by using the command
`path </path1> </path2> ...`. An important thing to note is that this command entry will overwrite all previous PATHs.

`exit` is used to exit the shell.

### Processes
Each shell command is given a separate child process to run on. This is extremely important in running external unix programs
because they replace the current process when they execute.

Additionally, this allows commands to run concurrently with the `&` operator like in the first command entry in the example below.

<div align="center">
  
  ![Screenshot 2023-10-28 173232](https://github.com/PaimonCodes/unix-shell-imitation/assets/104661175/1aa2223d-75c1-4fd7-8630-1d2b6598dac9)

</div>

---
### Project Idea and Test Cases Source
This project was based on Remzi Arpaci-Dusseau's OSTEP project series. 
GitHub: https://github.com/remzi-arpacidusseau/ostep-projects/tree/master/processes-shell

