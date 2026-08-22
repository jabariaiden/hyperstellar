#!/usr/bin/env python3
"""
HYPERSTELLAR GPU PHYSICS ENGINE - SPEED BENCHMARK TEST
Fixed to handle default object and use correct equation syntax
"""

import hyperstellar as hs
import numpy as np
import time
import math
from typing import List, Dict, Any
import json
import statistics
import gc
import traceback

# -----------------------------------------------------------------------------
# 1. CONFIGURATION
# -----------------------------------------------------------------------------

class SpeedTestConfig:
    """Configuration for speed benchmarks"""
    
    def __init__(self):
        # Object counts to test (powers of 2 for GPU performance)
        self.object_counts = [64, 128, 256, 512, 1024, 2048, 4096]
        
        # Time settings
        self.warmup_frames = 5      # Warmup frames
        self.measure_frames = 50    # Frames to measure
        self.dt = 0.016             # 60 FPS timestep
        
        # Performance targets (in milliseconds)
        self.target_60fps = 16.67   # 60 FPS
        self.target_30fps = 33.33   # 30 FPS
        
        # Test scenarios
        self.test_scenarios = {
            'empty': 'No objects (baseline)',
            'simple': 'Simple spring physics',
            'gravity': 'Gravity field',
            'complex': 'Complex equations',
            'interaction': 'Object interactions'
        }

# -----------------------------------------------------------------------------
# 2. HELPER FUNCTIONS
# -----------------------------------------------------------------------------

def clear_all_objects(sim):
    """Clear all objects from simulation (handle default object)"""
    # IMPORTANT: Always remove all objects starting from index 0
    # The while loop ensures we remove everything including the default
    while sim.object_count() > 0:
        try:
            sim.remove_object(0)
        except:
            break  # Break if we can't remove any more

def wait_for_shaders(sim):
    """Wait for shaders to load with progress display"""
    print("    Loading shaders...", end="", flush=True)
    start = time.time()
    
    while not sim.are_all_shaders_ready():
        progress = sim.get_shader_load_progress()
        sim.update_shader_loading()
        time.sleep(0.001)  # Very short sleep to not slow down
    
    load_time = time.time() - start
    print(f" {load_time:.2f}s")

def create_batch_data(sim, count=100):
    """Create batch update data"""
    updates = []
    for i in range(min(count, sim.object_count())):
        data = hs.BatchUpdateData()
        data.index = i
        data.x = np.random.uniform(-10, 10)
        data.y = np.random.uniform(-10, 10)
        data.vx = np.random.uniform(-0.5, 0.5)
        data.vy = np.random.uniform(-0.5, 0.5)
        data.mass = np.random.uniform(0.5, 5.0)
        updates.append(data)
    return updates

def print_speed_result(name, times_ms):
    """Print formatted speed result"""
    if not times_ms:
        print(f"    {name}: No data")
        return
    
    avg = statistics.mean(times_ms)
    std = statistics.stdev(times_ms) if len(times_ms) > 1 else 0
    fps = 1000 / avg if avg > 0 else 0
    
    status = "✅" if avg <= 16.67 else ("⚠️ " if avg <= 33.33 else "❌")
    
    print(f"    {status} {name}: {avg:.2f} ± {std:.2f} ms/frame ({fps:.0f} FPS)")

# -----------------------------------------------------------------------------
# 3. SPEED TEST SCENARIOS
# -----------------------------------------------------------------------------

def test_baseline_speed():
    """Test baseline speed with no objects"""
    print("\n1. BASELINE SPEED (No objects):")
    
    sim = hs.Simulation(headless=True, enable_grid=False)
    wait_for_shaders(sim)
    
    # Clear default object
    clear_all_objects(sim)
    
    # Warmup
    for _ in range(5):
        sim.update(0.016)
    
    # Measure
    times = []
    for _ in range(100):
        start = time.perf_counter()
        sim.update(0.016)
        times.append((time.perf_counter() - start) * 1000)
    
    print_speed_result("Empty simulation", times)
    
    sim.cleanup()
    return statistics.mean(times) if times else 0

def test_simple_physics_scaling():
    """Test speed scaling with simple physics equations"""
    print("\n2. SIMPLE PHYSICS SCALING:")
    print("   (Spring oscillator: -x - 0.1*vx, -y - 0.1*vy)")
    
    config = SpeedTestConfig()
    results = {}
    
    for count in config.object_counts:
        print(f"\n   Testing {count} objects:")
        
        sim = hs.Simulation(headless=True, enable_grid=False)
        wait_for_shaders(sim)
        
        try:
            # Clear default object
            clear_all_objects(sim)
            
            # Create objects with simple spring physics
            start_create = time.time()
            for i in range(count):
                angle = (i / count) * 2 * math.pi
                radius = 10.0 * (i / count)
                x = radius * math.cos(angle)
                y = radius * math.sin(angle)
                
                obj_id = sim.add_object(
                    x=x, y=y,
                    vx=np.random.uniform(-0.5, 0.5),
                    vy=np.random.uniform(-0.5, 0.5),
                    mass=np.random.uniform(0.5, 5.0),
                    skin=hs.SkinType.CIRCLE,
                    size=0.2
                )
                
                # Simple spring equation: -k*x - damping*vx
                sim.set_equation(obj_id, "-x - 0.1*vx, -y - 0.1*vy, 0, 1, 1, 1, 1")
            
            create_time = time.time() - start_create
            print(f"    Creation: {create_time:.3f}s ({count/create_time:.0f} obj/s)")
            
            # Warmup
            for _ in range(config.warmup_frames):
                sim.update(config.dt)
            
            # Measure
            frame_times = []
            for _ in range(config.measure_frames):
                start = time.perf_counter()
                sim.update(config.dt)
                frame_times.append((time.perf_counter() - start) * 1000)
            
            print_speed_result(f"{count} objects", frame_times)
            
            results[count] = {
                'create_time': create_time,
                'frame_times': frame_times,
                'avg_ms': statistics.mean(frame_times),
                'fps': 1000 / statistics.mean(frame_times) if statistics.mean(frame_times) > 0 else 0
            }
            
        finally:
            sim.cleanup()
            gc.collect()  # Force garbage collection
    
    return results

def test_gravity_field():
    """Test speed with gravity field equations"""
    print("\n3. GRAVITY FIELD PERFORMANCE:")
    print("   (Gravity: 0, -9.81)")
    
    config = SpeedTestConfig()
    results = {}
    
    for count in [256, 512, 1024, 2048]:  # Fewer tests for gravity
        print(f"\n   Testing {count} objects with gravity:")
        
        sim = hs.Simulation(headless=True, enable_grid=False)
        wait_for_shaders(sim)
        
        try:
            # Clear default object
            clear_all_objects(sim)
            
            # Create objects in a grid
            start_create = time.time()
            grid_size = int(math.sqrt(count))
            spacing = 20.0 / grid_size
            
            obj_ids = []
            for i in range(grid_size):
                for j in range(grid_size):
                    x = (i - grid_size/2) * spacing
                    y = (j - grid_size/2) * spacing
                    
                    obj_id = sim.add_object(
                        x=x, y=y,
                        vx=np.random.uniform(-0.1, 0.1),
                        vy=np.random.uniform(-0.1, 0.1),
                        mass=np.random.uniform(1.0, 3.0)
                    )
                    obj_ids.append(obj_id)
                    
                    # Simple gravity equation
                    sim.set_equation(obj_id, "0, -9.81, 0, 1, 1, 1, 1")
            
            create_time = time.time() - start_create
            print(f"    Creation: {create_time:.3f}s ({len(obj_ids)/create_time:.0f} obj/s)")
            
            # Warmup
            for _ in range(config.warmup_frames):
                sim.update(config.dt)
            
            # Measure
            frame_times = []
            for _ in range(config.measure_frames):
                start = time.perf_counter()
                sim.update(config.dt)
                frame_times.append((time.perf_counter() - start) * 1000)
            
            print_speed_result(f"{count} objects with gravity", frame_times)
            
            results[count] = {
                'create_time': create_time,
                'frame_times': frame_times,
                'avg_ms': statistics.mean(frame_times),
                'fps': 1000 / statistics.mean(frame_times)
            }
            
        finally:
            sim.cleanup()
            gc.collect()
    
    return results

def test_complex_equations():
    """Test speed with more complex equations"""
    print("\n4. COMPLEX EQUATIONS PERFORMANCE:")
    print("   (Using sin/cos functions and object references)")
    
    # Only test with fewer objects since equations are complex
    test_counts = [64, 128, 256]
    results = {}
    
    for count in test_counts:
        print(f"\n   Testing {count} objects with complex equations:")
        
        sim = hs.Simulation(headless=True, enable_grid=False)
        wait_for_shaders(sim)
        
        try:
            # Clear default object
            clear_all_objects(sim)
            
            # Create objects
            obj_ids = []
            for i in range(count):
                angle = (i / count) * 2 * math.pi
                radius = 5.0
                x = radius * math.cos(angle)
                y = radius * math.sin(angle)
                
                obj_id = sim.add_object(
                    x=x, y=y,
                    vx=0, vy=0,
                    mass=1.0
                )
                obj_ids.append(obj_id)
            
            # Set different types of complex equations
            start_eq_time = time.time()
            
            # Mix of equation types
            for i, obj_id in enumerate(obj_ids):
                if i % 4 == 0:
                    # Type 1: Spring with damping
                    sim.set_equation(obj_id, "-0.5*x - 0.05*vx, -0.5*y - 0.05*vy, 0, 1, 1, 1, 1")
                elif i % 4 == 1:
                    # Type 2: Orbit with sin/cos
                    sim.set_equation(obj_id, "sin(t)*0.1, cos(t)*0.1, 0, 1, 1, 1, 1")
                elif i % 4 == 2:
                    # Type 3: Attractor to center
                    sim.set_equation(obj_id, "-0.2*x/sqrt(x*x + y*y + 0.1), -0.2*y/sqrt(x*x + y*y + 0.1), 0, 1, 1, 1, 1")
                else:
                    # Type 4: Random force field
                    sim.set_equation(obj_id, "0.1*sin(x)*cos(y), 0.1*cos(x)*sin(y), 0, 1, 1, 1, 1")
            
            eq_time = time.time() - start_eq_time
            print(f"    Equation setup: {eq_time:.3f}s")
            
            # Warmup
            for _ in range(5):
                sim.update(0.016)
            
            # Measure
            frame_times = []
            for _ in range(50):
                start = time.perf_counter()
                sim.update(0.016)
                frame_times.append((time.perf_counter() - start) * 1000)
            
            print_speed_result(f"{count} complex objects", frame_times)
            
            results[count] = {
                'eq_time': eq_time,
                'frame_times': frame_times,
                'avg_ms': statistics.mean(frame_times),
                'fps': 1000 / statistics.mean(frame_times)
            }
            
        finally:
            sim.cleanup()
            gc.collect()
    
    return results

def test_batch_operations():
    """Test speed of batch operations vs individual operations"""
    print("\n5. BATCH OPERATIONS PERFORMANCE:")
    
    sim = hs.Simulation(headless=True, enable_grid=False)
    wait_for_shaders(sim)
    
    try:
        # Clear default object
        clear_all_objects(sim)
        
        # Create 1000 objects
        print("   Creating 1000 objects...")
        obj_ids = []
        start_create = time.time()
        
        for i in range(1000):
            obj_id = sim.add_object(
                x=np.random.uniform(-10, 10),
                y=np.random.uniform(-10, 10),
                mass=np.random.uniform(0.5, 5.0)
            )
            obj_ids.append(obj_id)
        
        create_time = time.time() - start_create
        print(f"    Individual creation: {create_time:.3f}s ({1000/create_time:.0f} obj/s)")
        
        # Test individual updates (100 objects)
        print("\n   Testing individual updates (100 objects):")
        individual_times = []
        for _ in range(20):
            start = time.perf_counter()
            for i in range(100):
                if i < len(obj_ids):
                    state = sim.get_object(obj_ids[i])
                    # Just getting the state counts as "update" for timing
            individual_times.append((time.perf_counter() - start) * 1000)
        
        print_speed_result("Individual get", individual_times)
        
        # Test batch get
        print("\n   Testing batch get (100 objects):")
        indices = obj_ids[:100]
        batch_get_times = []
        
        for _ in range(20):
            start = time.perf_counter()
            states = sim.batch_get(indices)
            batch_get_times.append((time.perf_counter() - start) * 1000)
        
        print_speed_result("Batch get", batch_get_times)
        
        # Test batch update
        print("\n   Testing batch update (100 objects):")
        updates = []
        for i, obj_id in enumerate(indices):
            data = hs.BatchUpdateData()
            data.index = obj_id
            data.x = np.random.uniform(-10, 10)
            data.y = np.random.uniform(-10, 10)
            updates.append(data)
        
        batch_update_times = []
        for _ in range(20):
            start = time.perf_counter()
            sim.batch_update(updates)
            batch_update_times.append((time.perf_counter() - start) * 1000)
        
        print_speed_result("Batch update", batch_update_times)
        
        # Calculate speedup
        ind_avg = statistics.mean(individual_times) if individual_times else 1
        batch_avg = statistics.mean(batch_get_times) if batch_get_times else 1
        speedup = ind_avg / batch_avg if batch_avg > 0 else 0
        
        print(f"\n    📊 Batch operations are {speedup:.1f}x faster than individual operations")
        
        return {
            'individual_get': individual_times,
            'batch_get': batch_get_times,
            'batch_update': batch_update_times,
            'speedup': speedup
        }
        
    finally:
        sim.cleanup()
        gc.collect()

def test_memory_usage():
    """Test memory usage scaling"""
    print("\n6. MEMORY USAGE SCALING:")
    
    import psutil
    process = psutil.Process()
    
    results = {}
    
    for count in [256, 1024, 4096]:
        print(f"\n   Testing {count} objects:")
        
        # Force garbage collection before test
        gc.collect()
        initial_mem = process.memory_info().rss / 1024 / 1024  # MB
        
        sim = hs.Simulation(headless=True, enable_grid=False)
        wait_for_shaders(sim)
        
        try:
            # Clear default object
            clear_all_objects(sim)
            
            # Create objects
            start_mem = process.memory_info().rss / 1024 / 1024
            
            for i in range(count):
                sim.add_object(
                    x=np.random.uniform(-10, 10),
                    y=np.random.uniform(-10, 10),
                    mass=np.random.uniform(0.5, 5.0)
                )
            
            after_create_mem = process.memory_info().rss / 1024 / 1024
            
            # Run simulation
            frame_mem = []
            for i in range(10):
                sim.update(0.016)
                if i % 2 == 0:
                    frame_mem.append(process.memory_info().rss / 1024 / 1024)
            
            after_sim_mem = process.memory_info().rss / 1024 / 1024
            
            # Calculate memory usage
            create_increase = after_create_mem - start_mem
            sim_increase = after_sim_mem - after_create_mem
            per_object_mb = create_increase / count if count > 0 else 0
            
            print(f"    Memory increase: {create_increase:.1f} MB ({per_object_mb:.3f} MB/object)")
            print(f"    During simulation: {sim_increase:.1f} MB change")
            
            if abs(sim_increase) < 5.0:
                print(f"    ✅ Stable memory usage")
            else:
                print(f"    ⚠️  Potential memory leak: {sim_increase:.1f} MB increase")
            
            results[count] = {
                'initial_mb': initial_mem,
                'after_create_mb': after_create_mem,
                'after_sim_mb': after_sim_mem,
                'create_increase_mb': create_increase,
                'sim_increase_mb': sim_increase,
                'mb_per_object': per_object_mb
            }
            
        finally:
            sim.cleanup()
            gc.collect()
    
    return results

def test_gpu_utilization():
    """Test GPU utilization by varying workload"""
    print("\n7. GPU UTILIZATION (Workload Scaling):")
    
    results = {}
    
    # Test with increasing equation complexity
    complexities = [
        ("Simple", "-0.1*x, -0.1*y, 0, 1, 1, 1, 1"),
        ("Medium", "-0.5*x - 0.05*vx + 0.1*sin(t), -0.5*y - 0.05*vy + 0.1*cos(t), 0, 1, 1, 1, 1"),
        ("Complex", "-x/(x*x+y*y+0.1) - 0.1*vx + 0.05*sin(t*x), -y/(x*x+y*y+0.1) - 0.1*vy + 0.05*cos(t*y), 0, 1, 1, 1, 1")
    ]
    
    for name, equation in complexities:
        print(f"\n   Testing {name} equations (256 objects):")
        
        sim = hs.Simulation(headless=True, enable_grid=False)
        wait_for_shaders(sim)
        
        try:
            # Clear default object
            clear_all_objects(sim)
            
            # Create 256 objects
            obj_ids = []
            for i in range(256):
                obj_id = sim.add_object(
                    x=np.random.uniform(-10, 10),
                    y=np.random.uniform(-10, 10),
                    mass=1.0
                )
                obj_ids.append(obj_id)
                sim.set_equation(obj_id, equation)
            
            # Warmup
            for _ in range(5):
                sim.update(0.016)
            
            # Measure
            frame_times = []
            for _ in range(30):
                start = time.perf_counter()
                sim.update(0.016)
                frame_times.append((time.perf_counter() - start) * 1000)
            
            avg_ms = statistics.mean(frame_times)
            fps = 1000 / avg_ms if avg_ms > 0 else 0
            
            print(f"    Performance: {avg_ms:.2f} ms/frame ({fps:.0f} FPS)")
            
            results[name] = {
                'equation': equation,
                'frame_times': frame_times,
                'avg_ms': avg_ms,
                'fps': fps
            }
            
        finally:
            sim.cleanup()
            gc.collect()
    
    return results

# -----------------------------------------------------------------------------
# 4. MAIN PERFORMANCE TEST
# -----------------------------------------------------------------------------

def run_speed_benchmark():
    """Run complete speed benchmark suite"""
    print("=" * 80)
    print("HYPERSTELLAR GPU PHYSICS ENGINE - SPEED BENCHMARK")
    print("=" * 80)
    print("\nTesting GPU acceleration and performance scaling...")
    
    all_results = {
        'timestamp': time.strftime("%Y-%m-%d %H:%M:%S"),
        'config': {
            'object_counts': [64, 128, 256, 512, 1024, 2048, 4096],
            'warmup_frames': 5,
            'measure_frames': 50,
            'dt_ms': 16.67
        },
        'tests': {}
    }
    
    try:
        # 1. Baseline
        baseline = test_baseline_speed()
        all_results['tests']['baseline'] = {'avg_ms': baseline}
        
        # 2. Simple physics scaling
        print("\n" + "=" * 80)
        print("MAIN PERFORMANCE SCALING TEST")
        print("=" * 80)
        simple_results = test_simple_physics_scaling()
        all_results['tests']['simple_scaling'] = simple_results
        
        # 3. Gravity field
        print("\n" + "=" * 80)
        gravity_results = test_gravity_field()
        all_results['tests']['gravity'] = gravity_results
        
        # 4. Complex equations
        print("\n" + "=" * 80)
        complex_results = test_complex_equations()
        all_results['tests']['complex'] = complex_results
        
        # 5. Batch operations
        print("\n" + "=" * 80)
        batch_results = test_batch_operations()
        all_results['tests']['batch'] = batch_results
        
        # 6. Memory usage
        print("\n" + "=" * 80)
        memory_results = test_memory_usage()
        all_results['tests']['memory'] = memory_results
        
        # 7. GPU utilization
        print("\n" + "=" * 80)
        gpu_results = test_gpu_utilization()
        all_results['tests']['gpu_utilization'] = gpu_results
        
        # Generate performance report
        generate_performance_report(all_results)
        
        return all_results
        
    except Exception as e:
        print(f"\n❌ Error during speed test: {e}")
        traceback.print_exc()
        return None

def generate_performance_report(results):
    """Generate comprehensive performance report"""
    print("\n" + "=" * 80)
    print("PERFORMANCE REPORT")
    print("=" * 80)
    
    if not results or 'tests' not in results:
        print("No results to report")
        return
    
    # Save detailed results
    with open('speed_benchmark_results.json', 'w') as f:
        json.dump(results, f, indent=2, default=lambda x: float(x) if isinstance(x, (np.float32, np.float64)) else str(x))
    print("Detailed results saved to: speed_benchmark_results.json")
    
    # Generate summary
    print("\n📊 PERFORMANCE SUMMARY:")
    print("-" * 80)
    
    # Baseline
    baseline = results['tests'].get('baseline', {}).get('avg_ms', 0)
    print(f"Baseline (empty): {baseline:.2f} ms")
    
    # Simple scaling analysis
    print("\n⚡ PERFORMANCE SCALING (Simple Physics):")
    print("Objects | Avg Frame Time | FPS      | Status")
    print("-" * 50)
    
    simple = results['tests'].get('simple_scaling', {})
    for count in sorted(simple.keys()):
        data = simple[count]
        avg_ms = data.get('avg_ms', 0)
        fps = data.get('fps', 0)
        
        if avg_ms <= 16.67:
            status = "✅ 60+ FPS"
        elif avg_ms <= 33.33:
            status = "⚠️  30-60 FPS"
        else:
            status = "❌ <30 FPS"
        
        print(f"{count:7d} | {avg_ms:13.2f} ms | {fps:7.0f} | {status}")
    
    # Batch operations speedup
    batch = results['tests'].get('batch', {})
    speedup = batch.get('speedup', 1.0)
    print(f"\n🔄 Batch operations: {speedup:.1f}x speedup over individual operations")
    
    # Memory efficiency
    memory = results['tests'].get('memory', {})
    if memory:
        print("\n💾 MEMORY EFFICIENCY:")
        for count in sorted(memory.keys()):
            data = memory[count]
            mb_per_obj = data.get('mb_per_object', 0)
            print(f"  {count} objects: {mb_per_obj:.3f} MB/object")
    
    # GPU utilization
    gpu = results['tests'].get('gpu_utilization', {})
    if gpu:
        print("\n🎮 GPU UTILIZATION:")
        for name, data in gpu.items():
            avg_ms = data.get('avg_ms', 0)
            fps = data.get('fps', 0)
            print(f"  {name:10} equations: {avg_ms:5.2f} ms ({fps:4.0f} FPS)")
    
    # Recommendations
    print("\n" + "=" * 80)
    print("RECOMMENDATIONS")
    print("=" * 80)
    
    # Find max objects for 60 FPS
    max_60fps = 0
    for count, data in simple.items():
        if data.get('avg_ms', 100) <= 16.67:
            max_60fps = max(max_60fps, count)
    
    if max_60fps >= 1000:
        print(f"✅ Excellent performance: Can simulate {max_60fps}+ objects at 60 FPS")
    elif max_60fps >= 500:
        print(f"⚠️  Good performance: Can simulate {max_60fps} objects at 60 FPS")
    else:
        print("⚠️  Performance limited: Consider optimization for larger simulations")
    
    if speedup >= 5.0:
        print("✅ Excellent batch operation efficiency")
    elif speedup >= 2.0:
        print("⚠️  Moderate batch efficiency - could be improved")
    
    print("\nFor best performance:")
    print("1. Use batch_update/batch_get for bulk operations")
    print("2. Keep equations simple for large object counts")
    print("3. Use headless mode for maximum speed")
    print("4. Monitor memory usage for very large simulations")
    
    # Save text summary
    with open('speed_summary.txt', 'w') as f:
        f.write("Hyperstellar Speed Benchmark Summary\n")
        f.write("=" * 50 + "\n\n")
        
        f.write(f"Test Date: {results['timestamp']}\n")
        f.write(f"Baseline: {baseline:.2f} ms\n\n")
        
        f.write("Performance Scaling:\n")
        f.write("-" * 40 + "\n")
        for count in sorted(simple.keys()):
            data = simple[count]
            avg_ms = data.get('avg_ms', 0)
            fps = data.get('fps', 0)
            f.write(f"{count} objects: {avg_ms:.2f} ms ({fps:.0f} FPS)\n")
        
        f.write(f"\nBatch Speedup: {speedup:.1f}x\n")
        
        if memory:
            f.write("\nMemory Efficiency:\n")
            f.write("-" * 40 + "\n")
            for count in sorted(memory.keys()):
                data = memory[count]
                mb_per_obj = data.get('mb_per_object', 0)
                f.write(f"{count} objects: {mb_per_obj:.3f} MB/object\n")
    
    print("\n📄 Summary saved to: speed_summary.txt")

# -----------------------------------------------------------------------------
# 5. QUICK PERFORMANCE CHECK
# -----------------------------------------------------------------------------

def quick_performance_check():
    """Quick performance check for development"""
    print("=" * 80)
    print("QUICK PERFORMANCE CHECK")
    print("=" * 80)
    
    sim = hs.Simulation(headless=True, enable_grid=False)
    
    try:
        # Wait for shaders
        print("Loading shaders...", end="", flush=True)
        while not sim.are_all_shaders_ready():
            sim.update_shader_loading()
        print(" ✅")
        
        # Clear default object
        clear_all_objects(sim)
        
        # Test with 1000 objects
        print("\nTesting with 1000 objects (simple spring physics)...")
        
        # Create objects
        start = time.time()
        for i in range(1000):
            sim.add_object(
                x=np.random.uniform(-10, 10),
                y=np.random.uniform(-10, 10),
                mass=np.random.uniform(0.5, 5.0)
            )
            sim.set_equation(i, "-0.1*x - 0.01*vx, -0.1*y - 0.01*vy, 0, 1, 1, 1, 1")
        
        create_time = time.time() - start
        print(f"  Creation time: {create_time:.2f}s ({1000/create_time:.0f} obj/s)")
        
        # Warmup
        for _ in range(5):
            sim.update(0.016)
        
        # Measure performance
        frame_times = []
        for i in range(30):
            start_frame = time.perf_counter()
            sim.update(0.016)
            frame_time = (time.perf_counter() - start_frame) * 1000
            frame_times.append(frame_time)
            
            if i % 10 == 0:
                print(f"  Frame {i}: {frame_time:.1f} ms")
        
        avg_time = statistics.mean(frame_times)
        fps = 1000 / avg_time
        
        print(f"\n  Average: {avg_time:.1f} ms ({fps:.0f} FPS)")
        
        if avg_time <= 16.67:
            print("  ✅ Excellent performance (60+ FPS)")
        elif avg_time <= 33.33:
            print("  ⚠️  Acceptable performance (30-60 FPS)")
        else:
            print("  ❌ Performance needs improvement (<30 FPS)")
        
        return avg_time <= 33.33  # Pass if 30+ FPS
        
    finally:
        sim.cleanup()

# -----------------------------------------------------------------------------
# 6. MAIN EXECUTION
# -----------------------------------------------------------------------------

if __name__ == "__main__":
    import sys
    
    print("Hyperstellar GPU Physics Engine - Speed Benchmark")
    print("Version: 1.0 | Focus: Performance Testing\n")
    
    if len(sys.argv) > 1:
        if sys.argv[1] == "--quick":
            # Quick performance check
            success = quick_performance_check()
            sys.exit(0 if success else 1)
        elif sys.argv[1] == "--simple":
            # Just run simple scaling test
            results = test_simple_physics_scaling()
            
            # Save results
            with open('simple_scaling.json', 'w') as f:
                json.dump(results, f, indent=2)
            print("\nResults saved to: simple_scaling.json")
            sys.exit(0)
    
    # Run full benchmark
    print("Running full performance benchmark suite...")
    print("This may take several minutes depending on your hardware.\n")
    
    results = run_speed_benchmark()
    
    if results:
        print("\n" + "=" * 80)
        print("✅ PERFORMANCE BENCHMARK COMPLETED SUCCESSFULLY!")
        print("=" * 80)
        sys.exit(0)
    else:
        print("\n" + "=" * 80)
        print("❌ PERFORMANCE BENCHMARK FAILED!")
        print("=" * 80)
        sys.exit(1)