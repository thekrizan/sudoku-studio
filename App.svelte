<script>
  import { onMount } from 'svelte';
  import { fly } from 'svelte/transition';

  // Runes keep the board, controls, messages, and solver results in sync.
  let mode = $state('solve');
  let difficulty = $state('easy');
  let puzzle = $state('');
  let board = $state(Array(81).fill(0));
  let givens = $state(Array(81).fill(false));
  let selected = $state(0);
  let keypadDismissed = $state(false);
  let busy = $state(false);
  let attempts = $state(0);
  let elapsed = $state(0);
  let message = $state('');
  let messageType = $state('success');
  let customInput = $state('');
  let invalidCells = $state([]);
  let animating = $state(false);
  let actionLabel = $state('Loading puzzle…');
  let solvedCount = $derived(board.filter(Boolean).length);

  const levels = ['easy', 'medium', 'hard', 'random', 'custom'];
  const fromDigits = (text) => [...text].map(Number);

  function notify(text = '', type = 'success') {
    message = text;
    messageType = type;
  }

  // All API work goes to CivetWeb. Sudoku rules are evaluated by its C handlers.
  async function request(path, options = {}) {
    let response;
    try {
      response = await fetch(path, options);
    } catch {
      throw new Error('C backend is unavailable. Start Sudoku Studio on 127.0.0.1:8080.');
    }
    let data;
    try {
      data = await response.json();
    } catch {
      throw new Error('The server returned an invalid response.');
    }
    if (!response.ok) throw new Error(data.message || data.status || `Request failed (${response.status})`);
    return data;
  }

  function post(path, text) {
    return request(path, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: text
    });
  }

  function setBusy(value, label = '') {
    busy = value;
    actionLabel = label || (mode === 'solve' ? 'Solve puzzle' : 'Check solution');
  }

  function loadPuzzle(data) {
    puzzle = data.puzzle;
    board = fromDigits(data.puzzle);
    givens = board.map(Boolean);
    invalidCells = [];
    animating = false;
    selected = Math.max(0, board.findIndex((value) => !value));
    keypadDismissed = false;
    attempts = 0;
    elapsed = 0;
    notify();
  }

  async function loadLevel(level = difficulty) {
    if (busy) return;
    difficulty = level;
    notify();
    if (level === 'custom') return;

    setBusy(true, 'Loading puzzle…');
    try {
      const data = level === 'random'
        ? await post('/api/generate', 'random')
        : await request(`/api/puzzle?difficulty=${encodeURIComponent(level)}`);
      loadPuzzle(data);
    } catch (error) {
      notify(error.message, 'failure');
    } finally {
      setBusy(false);
    }
  }

  async function loadCustom() {
    const cells = customInput.replace(/[\r\n]/g, '');
    if (cells.length !== 81 || !/^[0-9 .]{81}$/.test(cells)) {
      notify('Enter 81 cells using digits 1–9, zeroes, or blank spaces.', 'failure');
      return;
    }
    const normalized = cells.replace(/[ .]/g, '0');
    setBusy(true, 'Checking grid…');
    try {
      await post('/api/validate', normalized);
      difficulty = 'custom';
      loadPuzzle({ puzzle: normalized });
      notify('Custom puzzle loaded.');
    } catch (error) {
      notify(error.message === 'Clues conflict'
        ? 'Some clues conflict in a row, column, or box.'
        : error.message, 'failure');
    } finally {
      setBusy(false);
    }
  }

  async function solve() {
    if (!puzzle || busy) return;
    setBusy(true, 'Solving…');
    notify();
    try {
      const result = await post('/api/solve', board.join(''));
      if (result.status !== 'solved') throw new Error(result.message || 'This puzzle has no solution.');
      const answer = fromDigits(result.solution);
      attempts = result.candidate_attempts;
      elapsed = result.elapsed_ms;
      // Apply C's completed grid once; CSS staggers the blank-cell reveal.
      board = answer;
      animating = true;
      notify('Solved by the C backtracking engine.');
    } catch (error) {
      notify(error.message, 'failure');
    } finally {
      setBusy(false);
    }
  }

  // This local check only paints conflicting entries; the C API decides the result.
  function findConflicts() {
    const found = new Set();
    for (let first = 0; first < 81; first += 1) {
      if (!board[first]) continue;
      for (let second = first + 1; second < 81; second += 1) {
        const sameRow = Math.floor(first / 9) === Math.floor(second / 9);
        const sameColumn = first % 9 === second % 9;
        const sameBox = Math.floor(Math.floor(first / 9) / 3) === Math.floor(Math.floor(second / 9) / 3)
          && Math.floor((first % 9) / 3) === Math.floor((second % 9) / 3);
        if (board[first] === board[second] && (sameRow || sameColumn || sameBox)) {
          found.add(first);
          found.add(second);
        }
      }
    }
    return [...found];
  }

  async function checkSolution() {
    if (!puzzle || busy) return;
    setBusy(true, 'Checking…');
    notify();
    try {
      const result = await post('/api/check', puzzle + board.join(''));
      invalidCells = findConflicts();
      const outcomes = {
        correct: ['Correct! Every rule is satisfied.', 'success'],
        incorrect: ['Not quite. Check the entries and try again.', 'failure'],
        incomplete: ['The board still has empty cells.', 'failure'],
        invalid_input: ['The original puzzle contains conflicting clues.', 'failure']
      };
      const [text, type] = outcomes[result.status] || ['The board could not be checked.', 'failure'];
      notify(text, type);
    } catch (error) {
      notify(error.message, 'failure');
    } finally {
      setBusy(false);
    }
  }

  function clearEntries() {
    board = board.map((value, index) => givens[index] ? value : 0);
    invalidCells = [];
    notify();
  }

  function resetPuzzle() {
    board = fromDigits(puzzle);
    invalidCells = [];
    attempts = 0;
    elapsed = 0;
    notify();
  }

  function setSelectedValue(value) {
    if (busy || givens[selected]) return;
    board[selected] = value;
    board = [...board];
    invalidCells = [];
    notify();
  }

  // Keyboard input edits only non-clue cells and never steals typing from custom input.
  function enterDigit(event) {
    if (event.target instanceof HTMLTextAreaElement || busy) return;
    let next = selected;
    if (event.key === 'ArrowRight') next = Math.min(80, selected + 1);
    else if (event.key === 'ArrowLeft') next = Math.max(0, selected - 1);
    else if (event.key === 'ArrowDown') next = Math.min(80, selected + 9);
    else if (event.key === 'ArrowUp') next = Math.max(0, selected - 9);
    else if (/^[1-9]$/.test(event.key) && !givens[selected]) {
      setSelectedValue(Number(event.key));
      event.preventDefault();
      return;
    } else if (['Backspace', 'Delete', '0'].includes(event.key) && !givens[selected]) {
      setSelectedValue(0);
      event.preventDefault();
      return;
    } else return;
    selected = next;
    event.preventDefault();
  }

  function switchMode(nextMode) {
    mode = nextMode;
    notify();
    actionLabel = mode === 'solve' ? 'Solve puzzle' : 'Check solution';
  }

  onMount(() => loadLevel('easy'));
</script>

<svelte:window onkeydown={enterDigit} />

<main class="shell">
  <header class="topbar">
    <div class="brand"><span class="mark" aria-hidden="true">▦</span><span>Sudoku <b>Studio</b></span></div>
    <span class="local">LOCAL C ENGINE</span>
  </header>

  <section class="intro">
    <div>
      <p class="eyebrow">THINK IN NINES</p>
      <h1>A little logic.<br><em>A lot of satisfaction.</em></h1>
      <p class="lede">A calm place to solve, play, and check. Every puzzle rule lives in a small C engine running on your computer.</p>
    </div>
    <div class="mini-note"><span>01 / 02</span><p>Choose your mode<br>and make your next move.</p></div>
  </section>

  <section class="workspace" aria-label="Sudoku workspace">
    <div class="left">
      <div class="mode-tabs" role="tablist" aria-label="Game mode">
        <button class:active={mode === 'solve'} role="tab" aria-selected={mode === 'solve'} onclick={() => switchMode('solve')}>Auto Solve <small>let C find the answer</small></button>
        <button class:active={mode === 'play'} role="tab" aria-selected={mode === 'play'} onclick={() => switchMode('play')}>Play &amp; Check <small>solve it yourself</small></button>
      </div>

      <div class="board" role="grid" aria-label="Sudoku board">
        {#each board as value, index (index)}
          {@const row = Math.floor(index / 9)}
          {@const col = index % 9}
          {@const selectedRow = Math.floor(selected / 9)}
          {@const selectedCol = selected % 9}
          {@const peer = row === selectedRow || col === selectedCol || Math.floor(row / 3) === Math.floor(selectedRow / 3) && Math.floor(col / 3) === Math.floor(selectedCol / 3)}
          <button
            type="button"
            class="cell"
            class:given={givens[index]}
            class:entry={!givens[index] && value}
            class:selected={selected === index}
            class:peer={peer}
            class:invalid={invalidCells.includes(index)}
            class:reveal={animating && !givens[index]}
            class:rightbox={col === 2 || col === 5}
            class:bottombox={row === 2 || row === 5}
            style={`--reveal-delay:${index * 10}ms`}
            role="gridcell"
            aria-label={`Row ${row + 1}, column ${col + 1}, ${value || 'empty'}${givens[index] ? ', given' : ''}`}
            aria-selected={selected === index}
            onclick={() => { selected = index; keypadDismissed = false; }}
          >{value || ''}</button>
        {/each}
      </div>
      <div class="board-footer"><span><b>{solvedCount}</b> / 81 filled</span><span>Use arrow keys · 1–9 to enter</span></div>
      {#if !givens[selected] && !keypadDismissed}
        <section class="mobile-keypad" aria-label="Sudoku number keypad">
          <div class="keypad-heading">
            <span>ENTER A NUMBER</span>
            <button type="button" class="keypad-hide" onclick={() => keypadDismissed = true}>Hide keypad</button>
          </div>
          <div class="keypad-grid">
            {#each [1, 2, 3, 4, 5, 6, 7, 8, 9] as digit}
              <button type="button" class="keypad-button" aria-label={`Enter ${digit}`} disabled={busy || givens[selected]} onclick={() => setSelectedValue(digit)}>{digit}</button>
            {/each}
            <button type="button" class="keypad-button keypad-erase" disabled={busy || givens[selected]} onclick={() => setSelectedValue(0)}>Erase</button>
          </div>
        </section>
      {/if}
    </div>

    <aside class="controls">
      <div class="section-label">PUZZLE SOURCE</div>
      <div class="difficulty" aria-label="Puzzle difficulty">
        {#each levels as level}
          <button class:chosen={difficulty === level} onclick={() => loadLevel(level)} disabled={busy}>{level === 'random' ? 'Random ↻' : level[0].toUpperCase() + level.slice(1)}</button>
        {/each}
      </div>
      {#if difficulty === 'custom'}
        <div class="custom">
          <label for="custom-input">Paste 81 cells (0, space, or period = empty)</label>
          <textarea id="custom-input" bind:value={customInput} maxlength="89" placeholder="000000000..." spellcheck="false"></textarea>
          <button class="text-button" onclick={loadCustom} disabled={busy}>Load custom grid</button>
        </div>
      {/if}
      <div class="divider"></div>
      <div class="section-label">YOUR NEXT STEP</div>
      <p class="hint">{mode === 'solve' ? 'Let the C engine solve this board with recursive backtracking.' : 'Fill the empty cells, then ask the C engine to check your work.'}</p>
      <button class="primary" onclick={mode === 'solve' ? solve : checkSolution} disabled={busy || !puzzle}>
        <span>{busy ? actionLabel : mode === 'solve' ? 'Solve puzzle' : 'Check solution'}</span><span aria-hidden="true">↗</span>
      </button>
      <div class="secondary"><button onclick={resetPuzzle} disabled={!puzzle || busy}>Reset puzzle</button><button onclick={clearEntries} disabled={!puzzle || busy}>Clear entries</button></div>
      {#if message}
        <div class="result" class:success={messageType === 'success'} class:failure={messageType === 'failure'} role={messageType === 'failure' ? 'alert' : 'status'} transition:fly={{ y: 4, duration: 180 }}>
          <span>{messageType === 'success' ? '✓' : '!'}</span>
          <div>{message}{#if attempts}<small>{attempts.toLocaleString()} candidate attempts · {Number(elapsed).toFixed(2)} ms in C</small>{/if}</div>
        </div>
      {/if}
    </aside>
  </section>

  <footer><span>Sudoku Studio</span><span>Powered by a C backtracking engine · No account required</span></footer>
</main>
