#!/bin/bash

# Global counters for summary
cpp_count=0
h_count=0
makefile_count=0
total_lines=0

function print_tree() {
    local prefix="$1"
    local dir="$2"
    local first="$3"
    local last_dir="$4"

    # Print current directory name
    if [ "$dir" = "." ]; then
        echo "."
    else
        if [ "$first" = "true" ]; then
            echo "├── ${dir##*/}"
        else
            echo "${prefix}├── ${dir##*/}"
        fi
    fi

    # Get all items in directory
    local items=( $(ls -1 "$dir") )
    local total=${#items[@]}
    local count=0

    # Process each item
    for item in "${items[@]}"; do
        ((count++))
        local is_last=$([[ $count -eq $total ]] && echo "true" || echo "false")
        local full_path="$dir/$item"
        local new_prefix
        
        if [ "$first" = "true" ]; then
            new_prefix="    "
        else
            new_prefix="${prefix}│   "
        fi

        if [ -d "$full_path" ]; then
            # For directories, recurse
            if [ "$is_last" = "true" ]; then
                print_tree "$prefix│   " "$full_path" "false" "true"
            else
                print_tree "$prefix│   " "$full_path" "false" "false"
            fi
        elif [[ "$item" =~ \.(cpp|h)$ ]] || [[ "$item" == "Makefile" ]]; then
            # Count files and lines for summary
            local lines=$(wc -l < "$full_path")
            if [[ "$item" =~ \.cpp$ ]]; then
                ((cpp_count++))
            elif [[ "$item" =~ \.h$ ]]; then
                ((h_count++))
            elif [[ "$item" == "Makefile" ]]; then
                ((makefile_count++))
            fi
            ((total_lines+=lines))
            
            # Display file with line count
            if [ "$first" = "true" ]; then
                echo "├── $item - Lines: $lines"
            else
                echo "${prefix}├── $item - Lines: $lines"
            fi
        fi
    done
}

# Print tree structure
print_tree "" "." "true" "false"

# Print summary
echo -e "\nSummary:"
echo "-------------------"
echo "Total .cpp files: $cpp_count"
echo "Total .h files: $h_count"
echo "Total Makefiles: $makefile_count"
echo "Total lines: $total_lines"